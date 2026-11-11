/* Original AES-128, RFC4493 CMAC and RFC3394 key unwrap. */
#include <stdint.h>
#include "aes.h"
#include "crypto.h"
static unsigned char multiply(unsigned char a,unsigned char b)
{
    unsigned char r=0;for (unsigned i=0;i<8;++i) {
        r^=a&(unsigned char)(0u-(b&1));a=(unsigned char)((a<<1)^(0x1b&(0u-(a>>7))));b>>=1;
    }return r;
}
static unsigned char rotate(unsigned char a,unsigned n) { return (unsigned char)((a<<n)|(a>>(8-n))); }
static unsigned char inverse(unsigned char a)
{
    unsigned char r=1;for (unsigned bit=8;bit--;) { r=multiply(r,r);unsigned char t=multiply(r,a),mask=(unsigned char)(0u-((254u>>bit)&1));r=(r&~mask)|(t&mask); }return r;
}
static unsigned char sbox(unsigned char a) { unsigned char b=inverse(a);return b^rotate(b,1)^rotate(b,2)^rotate(b,3)^rotate(b,4)^0x63; }
static unsigned char invbox(unsigned char a) { return inverse(rotate(a,1)^rotate(a,3)^rotate(a,6)^0x05); }
static void expand(const unsigned char key[16],unsigned char rk[176])
{
    for (unsigned i=0;i<16;++i) { rk[i]=key[i]; }unsigned char rc=1,t[4];
    for (unsigned at=16;at<176;at+=4) {
        for (unsigned j=0;j<4;++j) { t[j]=rk[at-4+j]; }
        if (!(at%16)) { unsigned char first=t[0];t[0]=sbox(t[1])^rc;t[1]=sbox(t[2]);t[2]=sbox(t[3]);t[3]=sbox(first);rc=multiply(rc,2); }
        for (unsigned j=0;j<4;++j) { rk[at+j]=rk[at-16+j]^t[j]; }
    }crypto_wipe(t,sizeof(t));
}
static void block(const unsigned char rk[176],bool decrypt,const unsigned char in[16],unsigned char out[16])
{
    unsigned char s[16],t[16];for (unsigned i=0;i<16;++i) { s[i]=in[i]^rk[(decrypt ? 160 : 0)+i]; }
    for (unsigned round=1;round<=10;++round) {
        if (decrypt) {
            for (unsigned row=0;row<4;++row) { for (unsigned col=0;col<4;++col) { t[4*col+row]=invbox(s[4*((col+4-row)%4)+row]); } }
            for (unsigned i=0;i<16;++i) { s[i]=t[i]^rk[16*(10-round)+i]; }
        }else {
            for (unsigned row=0;row<4;++row) { for (unsigned col=0;col<4;++col) { t[4*col+row]=sbox(s[4*((col+row)%4)+row]); } }
            for (unsigned i=0;i<16;++i) { s[i]=t[i]; }
        }
        if (round<10) {
            for (unsigned col=0;col<4;++col) {
                unsigned char *p=s+4*col;
                for (unsigned row=0;row<4;++row) {
                    t[4*col+row]=decrypt ? multiply(p[row],14)^multiply(p[(row+1)%4],11)^multiply(p[(row+2)%4],13)^multiply(p[(row+3)%4],9) :
                        multiply(p[row],2)^multiply(p[(row+1)%4],3)^p[(row+2)%4]^p[(row+3)%4];
                }
            }for (unsigned i=0;i<16;++i) { s[i]=t[i]; }
        }
        if (!decrypt) { for (unsigned i=0;i<16;++i) { s[i]^=rk[16*round+i]; } }
    }
    for (unsigned i=0;i<16;++i) { out[i]=s[i]; }crypto_wipe(s,sizeof(s));crypto_wipe(t,sizeof(t));
}
static bool run(const unsigned char *key,const unsigned char *in,unsigned char *out,bool decrypt)
{
    if (!out) { return false; }if (!key || !in) { crypto_wipe(out,16);return false; }
    unsigned char rk[176];expand(key,rk);block(rk,decrypt,in,out);crypto_wipe(rk,sizeof(rk));return true;
}
bool aes128_encrypt(const unsigned char key[16],const unsigned char in[16],unsigned char out[16]) { return run(key,in,out,false); }
bool aes128_decrypt(const unsigned char key[16],const unsigned char in[16],unsigned char out[16]) { return run(key,in,out,true); }
static void double_key(unsigned char k[16])
{ unsigned top=k[0]>>7;for (unsigned i=0;i<15;++i) { k[i]=(unsigned char)((k[i]<<1)|(k[i+1]>>7)); }k[15]=(unsigned char)((k[15]<<1)^(0x87&(0u-top))); }
bool aes128_cmac(const unsigned char key[16],const void *message,size_t n,unsigned char out[16])
{
    if (!out) { return false; }if (!key || (!message && n) || n>4096) { crypto_wipe(out,16);return false; }
    struct { unsigned char rk[176],sub[16],acc[16],last[16]; } w={0};expand(key,w.rk);block(w.rk,false,w.sub,w.sub);double_key(w.sub);
    const unsigned char *p=message;size_t blocks=n ? (n+15)/16 : 1;
    for (size_t i=0;i+1<blocks;++i) { for (unsigned j=0;j<16;++j) { w.acc[j]^=p[16*i+j]; }block(w.rk,false,w.acc,w.acc); }
    size_t used=n ? n-16*(blocks-1) : 0;
    if (used<16) { double_key(w.sub);w.last[used]=0x80; }
    for (size_t j=0;j<used;++j) { w.last[j]=p[16*(blocks-1)+j]; }
    for (unsigned j=0;j<16;++j) { w.last[j]^=w.acc[j]^w.sub[j]; }block(w.rk,false,w.last,out);crypto_wipe(&w,sizeof(w));return true;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y ? y-x<an : x-y<bn); }
bool aes128_unwrap(const unsigned char key[16],const void *wrapped,size_t n,void *output,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }
    if (!output || !actual || !key || !wrapped || n<24 || n>520 || (n&7) || capacity<n-8 || capacity>512 || overlap(key,16,output,capacity) || overlap(wrapped,n,output,capacity)) {
        if (output && capacity<=512) { crypto_wipe(output,capacity); }return false;
    }
    struct { unsigned char rk[176],a[8],r[512],b[16]; } w={0};expand(key,w.rk);const unsigned char *p=wrapped;unsigned count=(unsigned)(n/8-1);
    for (unsigned i=0;i<8;++i) { w.a[i]=p[i]; }for (size_t i=8;i<n;++i) { w.r[i-8]=p[i]; }
    for (unsigned round=6;round--;) { for (unsigned i=count;i>0;--i) {
        uint64_t t=(uint64_t)count*round+i;for (unsigned j=0;j<8;++j) { w.b[j]=w.a[j]^(unsigned char)(t>>(8*(7-j)));w.b[8+j]=w.r[8*(i-1)+j]; }
        block(w.rk,true,w.b,w.b);for (unsigned j=0;j<8;++j) { w.a[j]=w.b[j];w.r[8*(i-1)+j]=w.b[8+j]; }
    } }
    unsigned diff=0;for (unsigned i=0;i<8;++i) { diff|=w.a[i]^0xa6; }bool ok=!diff;crypto_wipe(output,capacity);
    if (ok) { for (size_t i=0;i<n-8;++i) { ((unsigned char *)output)[i]=w.r[i]; }*actual=n-8; }
    crypto_wipe(&w,sizeof(w));return ok;
}
