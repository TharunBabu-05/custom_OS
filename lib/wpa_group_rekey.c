#include "wpa_group_rekey.h"
#include "aes.h"
#include "crypto.h"

static unsigned be16(const unsigned char *p)
{ return (unsigned)p[0]<<8|p[1]; }
static bool zero(const unsigned char *p,size_t n)
{ unsigned bits=0;for (size_t i=0;i<n;++i) { bits|=p[i]; }return !bits; }
static bool valid_mac(const unsigned char *p)
{ return !(p[0]&1) && !zero(p,6); }
static bool replay_newer(const unsigned char *value,const unsigned char *last)
{
    for (unsigned i=0;i<8;++i) { if (value[i]!=last[i]) { return value[i]>last[i]; } }
    return false;
}
static bool overlap(const void *a,size_t n,const void *b,size_t m)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return n && m && (x<=y ? y-x<n : x-y<m); }

enum wpa_group_result wpa_group_validate(const struct wpa_group_context *context,
    const void *frame,size_t n,struct wpa_group_offer *out)
{
    if (!out) { return WPA_GROUP_ARGUMENT; }
    if (!context || !frame || overlap(out,sizeof(*out),context,sizeof(*context)) ||
        overlap(out,sizeof(*out),frame,n)) { crypto_wipe(out,sizeof(*out));return WPA_GROUP_ARGUMENT; }
    crypto_wipe(out,sizeof(*out));
    if (!valid_mac(context->local) || !valid_mac(context->peer) || crypto_equal(context->local,context->peer,6) ||
        context->version<1 || context->version>3) { return WPA_GROUP_ARGUMENT; }
    if (n<113+24 || n>WPA_GROUP_FRAME_MAX) { return WPA_GROUP_FORMAT; }
    const unsigned char *p=frame,*e=p+14,*k=e+4;
    size_t wrapped=be16(k+93);
    if (!crypto_equal(p,context->local,6) || !crypto_equal(p+6,context->peer,6) ||
        be16(p+12)!=0x888e || e[0]!=context->version || e[1]!=3 || be16(e+2)!=n-18 ||
        k[0]!=2 || be16(k+1)!=0x1380 || be16(k+3) || !zero(k+45,16) || !zero(k+69,8) ||
        wrapped!=n-113 || wrapped%8) { return WPA_GROUP_FORMAT; }
    if (!replay_newer(k+5,context->last_replay)) { return WPA_GROUP_REPLAY; }

    unsigned char signed_bytes[99+520],mic[16],plain[512];
    for (size_t i=0;i<n-14;++i) { signed_bytes[i]=e[i]; }
    crypto_wipe(signed_bytes+81,16);
    bool verified=aes128_cmac(context->kck,signed_bytes,n-14,mic) && crypto_equal(mic,k+77,16);
    crypto_wipe(signed_bytes,sizeof(signed_bytes));crypto_wipe(mic,sizeof(mic));
    if (!verified) { return WPA_GROUP_MIC; }
    size_t bytes=0;
    if (!aes128_unwrap(context->kek,k+95,wrapped,plain,sizeof(plain),&bytes)) {
        crypto_wipe(plain,sizeof(plain));return WPA_GROUP_UNWRAP;
    }
    struct wpa_kde_info info;
    enum wpa_kde_result parsed=wpa_sae_group_data(plain,bytes,k+61,&info,&out->keys);
    crypto_wipe(plain,sizeof(plain));
    if (parsed!=KDE_OK) { return WPA_GROUP_KEY_DATA; }
    struct wpa_m4_input answer={.version=context->version};
    for (unsigned i=0;i<6;++i) { answer.local[i]=context->local[i];answer.ap[i]=context->peer[i]; }
    for (unsigned i=0;i<16;++i) { answer.kck[i]=context->kck[i]; }
    for (unsigned i=0;i<8;++i) { out->replay[i]=answer.replay[i]=k[5+i]; }
    bool encoded=wpa_sae_group2(&answer,out->reply,sizeof(out->reply),&bytes);
    crypto_wipe(&answer,sizeof(answer));
    if (encoded && bytes==sizeof(out->reply)) { return WPA_GROUP_OK; }
    crypto_wipe(out,sizeof(*out));return WPA_GROUP_ARGUMENT;
}
