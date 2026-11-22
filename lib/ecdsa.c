#include "ecdsa.h"
#include "p256.h"
#include <stdint.h>

struct scalar { uint32_t v[8]; };
static const struct scalar order={{0xfc632551,0xf3b9cac2,0xa7179e84,0xbce6faad,0xffffffff,0xffffffff,0,0xffffffff}};
static const struct scalar one={{1}};
static struct scalar decode(const unsigned char in[32])
{
    struct scalar out;
    for (unsigned i=0;i<8;++i) {
        const unsigned char *p=in+28-4*i;
        out.v[i]=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
    }
    return out;
}
static void encode(const struct scalar *value,unsigned char out[32])
{
    for (unsigned i=0;i<32;++i) { out[31-i]=(unsigned char)(value->v[i/4]>>(8*(i%4))); }
}
static uint32_t subtract(struct scalar *out,const struct scalar *a,const struct scalar *b)
{
    uint64_t borrow=0;
    for (unsigned i=0;i<8;++i) {
        uint64_t diff=(uint64_t)a->v[i]-b->v[i]-borrow;
        out->v[i]=(uint32_t)diff;borrow=diff>>63;
    }
    return (uint32_t)borrow;
}
static struct scalar reduce(const struct scalar *value)
{
    struct scalar diff;
    return subtract(&diff,value,&order) ? *value : diff;
}
static struct scalar add(const struct scalar *a,const struct scalar *b)
{
    struct scalar sum,diff;uint64_t carry=0;
    for (unsigned i=0;i<8;++i) {
        uint64_t n=(uint64_t)a->v[i]+b->v[i]+carry;
        sum.v[i]=(uint32_t)n;carry=n>>32;
    }
    uint32_t borrow=subtract(&diff,&sum,&order);
    return carry || !borrow ? diff : sum;
}
static bool bit(const struct scalar *s,unsigned i) { return (s->v[i/32]>>(i%32))&1; }
static struct scalar multiply(const struct scalar *a,const struct scalar *b)
{
    struct scalar result={0},power=*a;
    for (unsigned i=0;i<256;++i) {
        if (bit(b,i)) { result=add(&result,&power); }
        power=add(&power,&power);
    }
    return result;
}
static struct scalar invert(const struct scalar *s)
{
    struct scalar e=order,result=one;e.v[0]-=2;
    for (unsigned i=256;i--;) {
        result=multiply(&result,&result);
        if (bit(&e,i)) { result=multiply(&result,s); }
    }
    return result;
}
bool ecdsa_p256_verify(const unsigned char key[64],const unsigned char hash[32],const unsigned char r[32],const unsigned char s[32])
{
    if (!hash || !p256_scalar_valid(r) || !p256_scalar_valid(s) || !p256_point_valid(key)) { return false; }
    struct scalar z=decode(hash),rr=decode(r),ss=decode(s);
    z=reduce(&z);
    struct scalar inverse=invert(&ss),u1=multiply(&z,&inverse),u2=multiply(&rr,&inverse);
    unsigned char scalar[32],a[64],b[64];
    encode(&u2,scalar);if (!p256_multiply(scalar,key,b)) { return false; }
    encode(&u1,scalar);
    unsigned any=0;
    for (unsigned i=0;i<32;++i) { any|=scalar[i]; }
    if (any) {
        p256_generator(a);
        if (!p256_multiply(scalar,a,a) || !p256_add(a,b,b)) { return false; }
    }
    struct scalar x=decode(b);x=reduce(&x);
    uint32_t different=0;
    for (unsigned i=0;i<8;++i) { different|=x.v[i]^rr.v[i]; }
    return different==0;
}
