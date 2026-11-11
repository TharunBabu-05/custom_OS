/* Original bounded group-19 SAE hunting-and-pecking. Wire/KDF references:
 * hostap sae_derive_pwe_ecc()/sha256_prf_bits(), see docs/wifi-sae-hunt.md. */
#include <stdint.h>
#include "sae_hunt.h"
#include "p256.h"
#include "crypto.h"
static bool overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y ? y-x<an : x-y<bn); }
static bool mac(const unsigned char *p)
{ if (!p || (p[0]&1)) { return false; }unsigned any=0;for (unsigned i=0;i<6;++i) { any|=p[i]; }return !!any; }
bool sae_hunt_pwe(const void *password,size_t n,const unsigned char a[6],const unsigned char b[6],unsigned char out[64])
{
    if (!out) { return false; }
    if (!password || !n || n>128 || !mac(a) || !mac(b) || crypto_equal(a,b,6) || overlap(password,n,out,64) || overlap(a,6,out,64) || overlap(b,6,out,64)) {
        crypto_wipe(out,64);return false;
    }
    static const unsigned char prime[32]={0xff,0xff,0xff,0xff,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
    static const char label[]="SAE Hunting and Pecking";static const unsigned char one[2]={1,0},bits[2]={0,1};
    struct { unsigned char key[12],seed[32],value[32],point[64]; } w={0};
    int order=0;for (unsigned i=0;i<6 && !order;++i) { order=(int)a[i]-(int)b[i]; }
    for (unsigned i=0;i<6;++i) { w.key[i]=(order>0 ? a : b)[i];w.key[6+i]=(order>0 ? b : a)[i]; }
    crypto_wipe(out,64);unsigned found=0,healthy=1;
    for (unsigned counter=1;counter<=40;++counter) {
        unsigned char count=(unsigned char)counter;struct crypto_span seed[]={{password,n},{&count,1}};
        healthy&=(unsigned)hmac_sha256(w.key,12,seed,2,w.seed);
        struct crypto_span kdf[]={{one,2},{label,sizeof(label)-1},{prime,32},{bits,2}};
        healthy&=(unsigned)hmac_sha256(w.seed,32,kdf,4,w.value);
        unsigned valid=(unsigned)p256_from_x(w.value,w.seed[31]&1,w.point);
        unsigned char mask=(unsigned char)(0u-(valid&(found^1u)));
        for (unsigned i=0;i<64;++i) { out[i]=(unsigned char)((out[i]&~mask)|(w.point[i]&mask)); }found|=valid;
    }
    unsigned char mask=(unsigned char)(0u-(found&healthy));for (unsigned i=0;i<64;++i) { out[i]&=mask; }
    crypto_wipe(&w,sizeof(w));return !!(found&healthy);
}
