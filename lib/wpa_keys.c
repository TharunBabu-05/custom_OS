#include <stdint.h>
#include "wpa_keys.h"
#include "wpa_rsn.h"
#include "crypto.h"
#include "aes.h"
static bool overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y ? y-x<an : x-y<bn); }
static bool nonzero(const unsigned char *p,size_t n) { unsigned v=0;for (size_t i=0;i<n;++i) { v|=p[i]; }return !!v; }
static bool mac(const unsigned char *p) { return p && !(p[0]&1) && nonzero(p,6); }
static int compare(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return (int)a[i]-(int)b[i]; } }return 0; }
bool wpa_sae_ptk(const unsigned char pmk[32],const unsigned char local[6],const unsigned char ap[6],const unsigned char snonce[32],const unsigned char anonce[32],struct wpa_sae_ptk *out)
{
    if (!out) { return false; }
    if (!pmk || !mac(local) || !mac(ap) || crypto_equal(local,ap,6) || !snonce || !anonce || !nonzero(snonce,32) || !nonzero(anonce,32) || crypto_equal(snonce,anonce,32) ||
        overlap(pmk,32,out,sizeof(*out)) || overlap(local,6,out,sizeof(*out)) || overlap(ap,6,out,sizeof(*out)) || overlap(snonce,32,out,sizeof(*out)) || overlap(anonce,32,out,sizeof(*out))) { crypto_wipe(out,sizeof(*out));return false; }
    struct { unsigned char context[76],hash[32],keys[48]; } w={0};bool mac_order=compare(local,ap,6)<0,nonce_order=compare(snonce,anonce,32)<0;
    for (unsigned i=0;i<6;++i) { w.context[i]=(mac_order ? local : ap)[i];w.context[6+i]=(mac_order ? ap : local)[i]; }
    for (unsigned i=0;i<32;++i) { w.context[12+i]=(nonce_order ? snonce : anonce)[i];w.context[44+i]=(nonce_order ? anonce : snonce)[i]; }
    static const char label[]="Pairwise key expansion";static const unsigned char bits[2]={0x80,1};bool ok=true;
    for (unsigned i=0;i<2;++i) { unsigned char count[2]={(unsigned char)(i+1),0};struct crypto_span parts[]={{count,2},{label,sizeof(label)-1},{w.context,76},{bits,2}};
        ok=hmac_sha256(pmk,32,parts,4,w.hash) && ok;unsigned n=i ? 16 : 32;for (unsigned j=0;j<n;++j) { w.keys[32*i+j]=w.hash[j]; }
    }
    crypto_wipe(out,sizeof(*out));if (ok) { for (unsigned i=0;i<16;++i) { out->kck[i]=w.keys[i];out->kek[i]=w.keys[16+i];out->tk[i]=w.keys[32+i]; } }
    crypto_wipe(&w,sizeof(w));return ok;
}
static void be16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
bool wpa_sae_m4(const struct wpa_m4_input *in,void *output,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }
    if (!in || !output || !actual || !mac(in->local) || !mac(in->ap) || crypto_equal(in->local,in->ap,6) || in->version<1 || in->version>3 || capacity<113 || overlap(in,sizeof(*in),output,capacity)) { return false; }
    unsigned char packet[113]={0},mic[16];for (unsigned i=0;i<6;++i) { packet[i]=in->ap[i];packet[6+i]=in->local[i]; }be16(packet+12,0x888e);
    unsigned char *e=packet+14,*k=e+4;e[0]=(unsigned char)in->version;e[1]=3;be16(e+2,95);k[0]=2;be16(k+1,0x308);
    for (unsigned i=0;i<8;++i) { k[5+i]=in->replay[i]; }bool ok=aes128_cmac(in->kck,e,99,mic);
    if (ok) { for (unsigned i=0;i<16;++i) { k[77+i]=mic[i]; }for (unsigned i=0;i<113;++i) { ((unsigned char *)output)[i]=packet[i]; }*actual=113; }
    crypto_wipe(packet,sizeof(packet));crypto_wipe(mic,sizeof(mic));return ok;
}
bool wpa_sae_m2(const struct wpa_m2_input *in,void *output,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }
    if (!in || !output || !actual || !mac(in->local) || !mac(in->ap) || crypto_equal(in->local,in->ap,6) || !nonzero(in->snonce,32) || in->version<1 || in->version>3 || !in->rsn || in->rsn_bytes>257 || capacity<113+in->rsn_bytes || overlap(in,sizeof(*in),output,capacity) || overlap(in->rsn,in->rsn_bytes,output,capacity)) { return false; }
    struct wpa_rsn_info checked;if (wpa_sae_rsn(in->rsn,in->rsn_bytes,in->pmkid,&checked)!=RSN_OK) { return false; }
    unsigned char packet[370]={0},mic[16];size_t n=113+in->rsn_bytes;
    for (unsigned i=0;i<6;++i) { packet[i]=in->ap[i];packet[6+i]=in->local[i]; }be16(packet+12,0x888e);
    unsigned char *e=packet+14,*key=e+4;e[0]=(unsigned char)in->version;e[1]=3;be16(e+2,(unsigned)(95+in->rsn_bytes));key[0]=2;
    be16(key+1,0x108); /* pairwise + MIC, AKM-defined descriptor version 0 */
    /* RSN M2 key_length=0. Replay counter comes from M1; IV/RSC/key-ID/MIC
     * are zero before MIC generation over the EAPOL header/body only. */
    for (unsigned i=0;i<8;++i) { key[5+i]=in->replay[i]; }for (unsigned i=0;i<32;++i) { key[13+i]=in->snonce[i]; }
    be16(key+93,(unsigned)in->rsn_bytes);for (size_t i=0;i<in->rsn_bytes;++i) { key[95+i]=((const unsigned char *)in->rsn)[i]; }
    bool ok=aes128_cmac(in->kck,e,n-14,mic);
    if (ok) { for (unsigned i=0;i<16;++i) { key[77+i]=mic[i]; }for (size_t i=0;i<n;++i) { ((unsigned char *)output)[i]=packet[i]; }*actual=n; }
    crypto_wipe(packet,sizeof(packet));crypto_wipe(mic,sizeof(mic));return ok;
}
