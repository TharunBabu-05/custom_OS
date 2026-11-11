/* Original P-256 arithmetic for the next SAE stage. Curve/test parameters:
 * RFC5903 sections 3.1/8.1. Jacobian formulas: EFD dbl-2001-b/madd-2007-bl.
 * Fixed-size loops and masked exceptional-case handling at source level;
 * this is not a claim of a compiler/CPU side-channel audit. p256test uses
 * public known-answer values; the opt-in SAE commit probe uses private inputs. */
#include <stdint.h>
#include "p256.h"
#include "crypto.h"
typedef struct { uint32_t v[8]; } fe;
typedef struct { fe x,y,z; } jac;
static const fe prime={{0xffffffff,0xffffffff,0xffffffff,0,0,0,1,0xffffffff}};
static const fe order={{0xfc632551,0xf3b9cac2,0xa7179e84,0xbce6faad,0xffffffff,0xffffffff,0,0xffffffff}};
static const fe curve_b={{0x27d2604b,0x3bce3c3e,0xcc53b0f6,0x651d06b0,0x769886bc,0xb3ebbd55,0xaa3a93e7,0x5ac635d8}};
static const fe one={{1}},zero={{0}};
static uint32_t nonzero(const fe *a)
{ uint32_t v=0;for (unsigned i=0;i<8;++i) { v|=a->v[i]; }return (v|(0u-v))>>31; }
static void select_fe(fe *r,const fe *a,const fe *b,uint32_t bit)
{ uint32_t mask=0u-bit;for (unsigned i=0;i<8;++i) { r->v[i]=(a->v[i]&~mask)|(b->v[i]&mask); } }
static uint32_t subtract(fe *r,const fe *a,const fe *b)
{
    uint64_t borrow=0;
    for (unsigned i=0;i<8;++i) { uint64_t x=a->v[i],y=(uint64_t)b->v[i]+borrow;r->v[i]=(uint32_t)(x-y);borrow=x<y; }
    return (uint32_t)borrow;
}
static void decode(fe *r,const unsigned char *p)
{
    for (unsigned i=0;i<8;++i) { unsigned at=28-4*i;r->v[i]=(uint32_t)p[at]<<24|(uint32_t)p[at+1]<<16|(uint32_t)p[at+2]<<8|p[at+3]; }
}
static void encode(unsigned char *p,const fe *a)
{ for (unsigned i=0;i<8;++i) { for (unsigned j=0;j<4;++j) { p[31-4*i-j]=(unsigned char)(a->v[i]>>(8*j)); } } }
static void add(fe *r,const fe *a,const fe *b)
{
    fe sum,difference;uint64_t carry=0;
    for (unsigned i=0;i<8;++i) { uint64_t v=(uint64_t)a->v[i]+b->v[i]+carry;sum.v[i]=(uint32_t)v;carry=v>>32; }
    uint32_t borrow=subtract(&difference,&sum,&prime);select_fe(r,&sum,&difference,(uint32_t)carry|(borrow^1u));
    crypto_wipe(&sum,sizeof(sum));crypto_wipe(&difference,sizeof(difference));
}
static void sub(fe *r,const fe *a,const fe *b)
{
    fe difference;uint32_t borrow=subtract(&difference,a,b),mask=0u-borrow;uint64_t carry=0;
    for (unsigned i=0;i<8;++i) { uint64_t v=(uint64_t)difference.v[i]+(prime.v[i]&mask)+carry;r->v[i]=(uint32_t)v;carry=v>>32; }
    crypto_wipe(&difference,sizeof(difference));
}
static void multiply(fe *r,const fe *a,const fe *b)
{
    uint32_t product[16]={0};fe rem,difference;int64_t folded[16];
    for (unsigned i=0;i<8;++i) {
        uint64_t carry=0;
        for (unsigned j=0;j<8;++j) { uint64_t v=(uint64_t)a->v[i]*b->v[j]+product[i+j]+carry;product[i+j]=(uint32_t)v;carry=v>>32; }
        product[i+8]=(uint32_t)carry;
    }
    /* B=2^32, p=B^8-B^7+B^6+B^3-1, so B^8 = B^7-B^6-B^3+1
     * modulo p. Fold eight high limbs in descending order, with signed
     * accumulators (each is bounded by 9*(B-1), far below INT64_MAX).
     * The initial normalized carry is in [-8,8]. Folding that carry gives
     * at most +/-1; a second fold cannot overflow: its residue is near the
     * opposite end of [0,B^8). Four fixed normalization rounds include one
     * spare round. All products take the same public loop counts. */
    for (unsigned i=0;i<16;++i) { folded[i]=product[i]; }
    for (unsigned k=16;k-->8;) {
        int64_t high=folded[k];folded[k]=0;
        folded[k-8]+=high;folded[k-1]+=high;folded[k-2]-=high;folded[k-5]-=high;
    }
    for (unsigned round=0;round<4;++round) {
        int64_t carry=0;
        for (unsigned i=0;i<8;++i) {
            int64_t value=folded[i]+carry;uint32_t low=(uint32_t)value;
            /* Exact division by constant 2^32; avoids implementation-defined
             * right shift of negative signed values. No secret divisor. */
            carry=(value-(int64_t)low)/INT64_C(4294967296);folded[i]=low;
        }
        folded[0]+=carry;folded[7]+=carry;folded[6]-=carry;folded[3]-=carry;
    }
    for (unsigned i=0;i<8;++i) { rem.v[i]=(uint32_t)folded[i]; }
    uint32_t borrow=subtract(&difference,&rem,&prime);select_fe(r,&rem,&difference,borrow^1u);
    crypto_wipe(product,sizeof(product));crypto_wipe(folded,sizeof(folded));crypto_wipe(&rem,sizeof(rem));crypto_wipe(&difference,sizeof(difference));
}
#ifdef OUROS_P256_FIELD_TEST
/* Native-only differential test seam; absent from the kernel API/build. */
void p256_field_product_test(const unsigned char a[32],const unsigned char b[32],unsigned char out[32]);
void p256_field_product_test(const unsigned char a[32],const unsigned char b[32],unsigned char out[32])
{ fe x,y,r;decode(&x,a);decode(&y,b);multiply(&r,&x,&y);encode(out,&r);crypto_wipe(&x,sizeof(x));crypto_wipe(&y,sizeof(y));crypto_wipe(&r,sizeof(r)); }
#endif
static void square(fe *r,const fe *a) { multiply(r,a,a); }
static void twice(fe *r,const fe *a) { add(r,a,a); }
static void power(fe *r,const fe *a,const fe *exponent)
{
    fe value=one,product;
    for (unsigned bit=256;bit--;) {
        square(&value,&value);multiply(&product,&value,a);
        select_fe(&value,&value,&product,(exponent->v[bit/32]>>(bit%32))&1);
    }
    *r=value;crypto_wipe(&value,sizeof(value));crypto_wipe(&product,sizeof(product));
}
static void inverse(fe *r,const fe *a)
{
    /* Fermat: p-2, including inv0(0)=0 for SSWU. */
    static const fe exponent={{0xfffffffd,0xffffffff,0xffffffff,0,0,0,1,0xffffffff}};
    power(r,a,&exponent);
}
static bool valid_affine(const fe *x,const fe *y)
{
    fe difference,left,right,t;
    uint32_t canonical=subtract(&difference,x,&prime);canonical&=subtract(&difference,y,&prime);
    square(&left,y);square(&right,x);multiply(&right,&right,x);
    twice(&t,x);add(&t,&t,x);sub(&right,&right,&t);add(&right,&right,&curve_b);sub(&difference,&left,&right);
    bool valid=canonical && !nonzero(&difference);
    crypto_wipe(&difference,sizeof(difference));crypto_wipe(&left,sizeof(left));crypto_wipe(&right,sizeof(right));crypto_wipe(&t,sizeof(t));return valid;
}
static void select_point(jac *r,const jac *a,const jac *b,uint32_t bit)
{ select_fe(&r->x,&a->x,&b->x,bit);select_fe(&r->y,&a->y,&b->y,bit);select_fe(&r->z,&a->z,&b->z,bit); }
static void double_point(jac *r,const jac *p)
{
    struct { fe delta,gamma,beta,alpha,t,u; jac result; } w;
    square(&w.delta,&p->z);square(&w.gamma,&p->y);multiply(&w.beta,&p->x,&w.gamma);
    sub(&w.t,&p->x,&w.delta);add(&w.u,&p->x,&w.delta);multiply(&w.alpha,&w.t,&w.u);
    twice(&w.t,&w.alpha);add(&w.alpha,&w.t,&w.alpha);
    twice(&w.t,&w.beta);twice(&w.t,&w.t);twice(&w.u,&w.t);
    square(&w.result.x,&w.alpha);sub(&w.result.x,&w.result.x,&w.u);
    sub(&w.t,&w.t,&w.result.x);multiply(&w.result.y,&w.alpha,&w.t);
    square(&w.u,&w.gamma);twice(&w.u,&w.u);twice(&w.u,&w.u);twice(&w.u,&w.u);sub(&w.result.y,&w.result.y,&w.u);
    add(&w.t,&p->y,&p->z);square(&w.result.z,&w.t);sub(&w.result.z,&w.result.z,&w.gamma);sub(&w.result.z,&w.result.z,&w.delta);
    *r=w.result;crypto_wipe(&w,sizeof(w));
}
static void add_affine(jac *r,const jac *p,const jac *q)
{
    /* q.z == 1. Compute exceptional candidates unconditionally and select.
     * H=0,r=0 -> doubling; H=0,r!=0 -> infinity (Z=0); P=infinity -> Q. */
    struct { fe z2,u,s,h,hh,i,j,rr,v,t; jac result,doubled; } w;
    square(&w.z2,&p->z);multiply(&w.u,&q->x,&w.z2);multiply(&w.s,&q->y,&p->z);multiply(&w.s,&w.s,&w.z2);
    sub(&w.h,&w.u,&p->x);square(&w.hh,&w.h);twice(&w.i,&w.hh);twice(&w.i,&w.i);multiply(&w.j,&w.h,&w.i);
    sub(&w.rr,&w.s,&p->y);twice(&w.rr,&w.rr);multiply(&w.v,&p->x,&w.i);
    square(&w.result.x,&w.rr);sub(&w.result.x,&w.result.x,&w.j);twice(&w.t,&w.v);sub(&w.result.x,&w.result.x,&w.t);
    sub(&w.t,&w.v,&w.result.x);multiply(&w.result.y,&w.rr,&w.t);multiply(&w.t,&p->y,&w.j);twice(&w.t,&w.t);sub(&w.result.y,&w.result.y,&w.t);
    add(&w.t,&p->z,&w.h);square(&w.result.z,&w.t);sub(&w.result.z,&w.result.z,&w.z2);sub(&w.result.z,&w.result.z,&w.hh);
    double_point(&w.doubled,p);select_point(&w.result,&w.result,&w.doubled,(nonzero(&w.h)^1u)&(nonzero(&w.rr)^1u));
    select_point(&w.result,&w.result,q,nonzero(&p->z)^1u);*r=w.result;crypto_wipe(&w,sizeof(w));
}
static bool export_point(const jac *p,unsigned char *output)
{
    fe z,x,y,t;bool valid=false;
    if (nonzero(&p->z)) {
        inverse(&z,&p->z);square(&t,&z);multiply(&x,&p->x,&t);multiply(&t,&t,&z);multiply(&y,&p->y,&t);
        valid=valid_affine(&x,&y);if (valid) { encode(output,&x);encode(output+32,&y); }
    }
    if (!valid) { crypto_wipe(output,64); }
    crypto_wipe(&z,sizeof(z));crypto_wipe(&x,sizeof(x));crypto_wipe(&y,sizeof(y));crypto_wipe(&t,sizeof(t));return valid;
}
void p256_generator(unsigned char point[64])
{
    static const unsigned char generator[64]={
        0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96,
        0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5};
    if (point) { for (unsigned i=0;i<64;++i) { point[i]=generator[i]; } }
}
bool p256_scalar_valid(const unsigned char scalar[32])
{
    if (!scalar) { return false; }fe s,d;decode(&s,scalar);bool valid=!!(subtract(&d,&s,&order)&nonzero(&s));
    crypto_wipe(&s,sizeof(s));crypto_wipe(&d,sizeof(d));return valid;
}
bool p256_point_valid(const unsigned char point[64])
{
    if (!point) { return false; }fe x,y;decode(&x,point);decode(&y,point+32);bool valid=valid_affine(&x,&y);
    crypto_wipe(&x,sizeof(x));crypto_wipe(&y,sizeof(y));return valid;
}
bool p256_multiply(const unsigned char scalar[32],const unsigned char point[64],unsigned char output[64])
{
    if (!output) { return false; }struct { fe scalar;jac base,accum,doubled,added; } w={0};bool ok=false;
    if (scalar && point) {
        decode(&w.scalar,scalar);decode(&w.base.x,point);decode(&w.base.y,point+32);w.base.z=one;
        ok=p256_scalar_valid(scalar) && valid_affine(&w.base.x,&w.base.y);
    }
    crypto_wipe(output,64);
    if (ok) {
        for (unsigned bit=256;bit--;) {
            double_point(&w.doubled,&w.accum);add_affine(&w.added,&w.doubled,&w.base);
            select_point(&w.accum,&w.doubled,&w.added,(w.scalar.v[bit/32]>>(bit%32))&1);
        }
        ok=export_point(&w.accum,output);
    }
    crypto_wipe(&w,sizeof(w));return ok;
}
bool p256_add(const unsigned char a[64],const unsigned char b[64],unsigned char output[64])
{
    if (!output) { return false; }struct { jac a,b,result; } w={0};bool ok=false;
    if (a && b) { decode(&w.a.x,a);decode(&w.a.y,a+32);decode(&w.b.x,b);decode(&w.b.y,b+32);w.a.z=one;w.b.z=one;ok=valid_affine(&w.a.x,&w.a.y) && valid_affine(&w.b.x,&w.b.y); }
    crypto_wipe(output,64);if (ok) { add_affine(&w.result,&w.a,&w.b);ok=export_point(&w.result,output); }
    crypto_wipe(&w,sizeof(w));return ok;
}
bool p256_negate(const unsigned char point[64],unsigned char output[64])
{
    if (!output) { return false; }fe x={{0}},y={{0}};bool ok=false;
    if (point) { decode(&x,point);decode(&y,point+32);ok=valid_affine(&x,&y); }
    crypto_wipe(output,64);if (ok) { sub(&y,&zero,&y);encode(output,&x);encode(output+32,&y); }
    crypto_wipe(&x,sizeof(x));crypto_wipe(&y,sizeof(y));return ok;
}
bool p256_scalar_from_hash(const unsigned char hash[32],unsigned char output[32])
{
    if (!output) { return false; }if (!hash) { crypto_wipe(output,32);return false; }
    static const fe modulus={{0xfc632550,0xf3b9cac2,0xa7179e84,0xbce6faad,0xffffffff,0xffffffff,0,0xffffffff}};
    fe value,difference;decode(&value,hash);uint32_t borrow=subtract(&difference,&value,&modulus);
    select_fe(&value,&value,&difference,borrow^1u);uint64_t carry=1;
    for (unsigned i=0;i<8;++i) { uint64_t v=(uint64_t)value.v[i]+carry;value.v[i]=(uint32_t)v;carry=v>>32; }
    encode(output,&value);crypto_wipe(&value,sizeof(value));crypto_wipe(&difference,sizeof(difference));return true;
}
static void curve_value(fe *r,const fe *x)
{
    fe t;square(r,x);multiply(r,r,x);twice(&t,x);add(&t,&t,x);sub(r,r,&t);add(r,r,&curve_b);crypto_wipe(&t,sizeof(t));
}
bool p256_from_x(const unsigned char x[32],unsigned parity,unsigned char output[64])
{
    if (!output) { return false; }if (!x || parity>1) { crypto_wipe(output,64);return false; }
    static const fe exponent={{0,0,0x40000000,0,0,0x40000000,0xc0000000,0x3fffffff}};
    struct { fe x,y,g,check,diff; } w={0};decode(&w.x,x);
    uint32_t canonical=subtract(&w.diff,&w.x,&prime);select_fe(&w.x,&zero,&w.x,canonical);
    curve_value(&w.g,&w.x);power(&w.y,&w.g,&exponent);square(&w.check,&w.y);sub(&w.diff,&w.check,&w.g);
    uint32_t valid=canonical&(nonzero(&w.diff)^1u);
    sub(&w.diff,&zero,&w.y);select_fe(&w.y,&w.y,&w.diff,(w.y.v[0]^parity)&1u);
    encode(output,&w.x);encode(output+32,&w.y);unsigned char mask=(unsigned char)(0u-valid);
    for (unsigned i=0;i<64;++i) { output[i]&=mask; }
    crypto_wipe(&w,sizeof(w));return !!valid;
}
bool p256_scalar_sum(const unsigned char a[32],const unsigned char b[32],unsigned char output[32])
{
    if (!output) { return false; }
    if (!a || !b) { crypto_wipe(output,32);return false; }
    struct { fe a,b,sum,difference; } w;decode(&w.a,a);decode(&w.b,b);
    uint32_t valid_a=subtract(&w.difference,&w.a,&order)&nonzero(&w.a);
    uint32_t valid_b=subtract(&w.difference,&w.b,&order)&nonzero(&w.b);bool ok=!!(valid_a&valid_b);
    crypto_wipe(output,32);
    if (ok) {
        uint64_t carry=0;
        for (unsigned i=0;i<8;++i) { uint64_t v=(uint64_t)w.a.v[i]+w.b.v[i]+carry;w.sum.v[i]=(uint32_t)v;carry=v>>32; }
        uint32_t borrow=subtract(&w.difference,&w.sum,&order);select_fe(&w.sum,&w.sum,&w.difference,(uint32_t)carry|(borrow^1u));encode(output,&w.sum);
    }
    crypto_wipe(&w,sizeof(w));return ok;
}
bool p256_sswu(const unsigned char value[48],unsigned char output[64])
{
    if (!output) { return false; }if (!value) { crypto_wipe(output,64);return false; }
    /* Group-19 constants: -B/A=B/3, B/(Z*A)=B/30, A=-3, Z=-10. */
    static const fe c1={{0x629b756e,0x6944bebf,0x441be5a7,0xcc5f023b,0x7cdd823e,0x3bf93f1c,0xe368dbf8,0x73976747}};
    static const fe c2={{0x2375f224,0xbdba1313,0x20693090,0x146fe6a0,0xd94959d3,0x6c65b982,0x96bdaf99,0xa528bd86}};
    static const fe sqrt_exp={{0,0,0x40000000,0,0,0x40000000,0xc0000000,0x3fffffff}};
    static const fe ten={{10}};
    struct { fe u,diff,z,t,m,x1,x2,g1,g2,y1,y2,x,y,check; } w={0};
    /* Fixed 384-bit reduction, before wiping a potentially overlapping output. */
    for (unsigned bit=384;bit--;) {
        uint32_t carry=(value[47-bit/8]>>(bit%8))&1;
        for (unsigned i=0;i<8;++i) { uint32_t next=w.u.v[i]>>31;w.u.v[i]=(w.u.v[i]<<1)|carry;carry=next; }
        uint32_t borrow=subtract(&w.diff,&w.u,&prime);select_fe(&w.u,&w.u,&w.diff,carry|(borrow^1u));
    }
    crypto_wipe(output,64);sub(&w.z,&zero,&ten);square(&w.t,&w.u);multiply(&w.t,&w.t,&w.z);
    square(&w.m,&w.t);add(&w.m,&w.m,&w.t);uint32_t exceptional=nonzero(&w.m)^1u;
    inverse(&w.x1,&w.m);add(&w.x1,&w.x1,&one);multiply(&w.x1,&w.x1,&c1);select_fe(&w.x1,&w.x1,&c2,exceptional);
    multiply(&w.x2,&w.t,&w.x1);curve_value(&w.g1,&w.x1);curve_value(&w.g2,&w.x2);
    /* Always compute both roots. Their squares determine which branch is valid. */
    power(&w.y1,&w.g1,&sqrt_exp);power(&w.y2,&w.g2,&sqrt_exp);
    square(&w.check,&w.y1);sub(&w.diff,&w.check,&w.g1);uint32_t use_second=nonzero(&w.diff);
    select_fe(&w.x,&w.x1,&w.x2,use_second);select_fe(&w.y,&w.y1,&w.y2,use_second);
    sub(&w.diff,&zero,&w.y);select_fe(&w.y,&w.y,&w.diff,(w.u.v[0]^w.y.v[0])&1);
    bool ok=valid_affine(&w.x,&w.y);if (ok) { encode(output,&w.x);encode(output+32,&w.y); }
    crypto_wipe(&w,sizeof(w));return ok;
}
