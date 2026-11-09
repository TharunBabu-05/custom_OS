/* Original SAE H2E group-19 derivation. Protocol reference: hostap sae.c
 * sae_derive_pt_ecc()/sae_derive_pwe_from_pt_ecc(); SSWU tested with RFC9380.
 * Current monitor calls use synthetic public fixtures only, not credentials. */
#include <stdint.h>
#include "sae_h2e.h"
#include "p256.h"
#include "crypto.h"
static bool overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y ? y-x<an : x-y<bn); }
bool sae_h2e_pt(const void *ssid,size_t sn,const void *password,size_t pn,const void *id,size_t in,unsigned char output[64])
{
    if (!output) { return false; }
    if (!ssid || !sn || sn>32 || !password || !pn || pn>128 || (!id && in) || in>64 || overlap(ssid,sn,output,64) || overlap(password,pn,output,64) || overlap(id,in,output,64)) { crypto_wipe(output,64);return false; }
    static const char label1[]="SAE Hash to Element u1 P1",label2[]="SAE Hash to Element u2 P2";
    struct { unsigned char seed[32],value[48],p1[64],p2[64]; } w={0};struct crypto_span parts[]={{password,pn},{id,in}};
    crypto_wipe(output,64);
    bool ok=hmac_sha256(ssid,sn,parts,2,w.seed) &&
        hkdf_sha256_expand(w.seed,label1,sizeof(label1)-1,w.value,48) && p256_sswu(w.value,w.p1) &&
        hkdf_sha256_expand(w.seed,label2,sizeof(label2)-1,w.value,48) && p256_sswu(w.value,w.p2) &&
        p256_add(w.p1,w.p2,output);
    crypto_wipe(&w,sizeof(w));if (!ok) { crypto_wipe(output,64); }return ok;
}
static bool valid_mac(const unsigned char *mac)
{ if (!mac || (mac[0]&1)) { return false; }unsigned any=0;for (unsigned i=0;i<6;++i) { any|=mac[i]; }return any!=0; }
bool sae_h2e_pwe(const unsigned char pt[64],const unsigned char a[6],const unsigned char b[6],unsigned char output[64])
{
    if (!output) { return false; }
    if (!pt || !valid_mac(a) || !valid_mac(b) || crypto_equal(a,b,6) || overlap(pt,64,output,64) || overlap(a,6,output,64) || overlap(b,6,output,64)) { crypto_wipe(output,64);return false; }
    crypto_wipe(output,64);if (!p256_point_valid(pt)) { return false; }
    int order=0;for (unsigned i=0;i<6 && !order;++i) { order=(int)a[i]-(int)b[i]; }
    struct { unsigned char zero[32],hash[32],scalar[32]; } w={0};
    struct crypto_span parts[]={{order>0 ? a : b,6},{order>0 ? b : a,6}};
    bool ok=hmac_sha256(w.zero,32,parts,2,w.hash) && p256_scalar_from_hash(w.hash,w.scalar) && p256_multiply(w.scalar,pt,output);
    crypto_wipe(&w,sizeof(w));if (!ok) { crypto_wipe(output,64); }return ok;
}
