#include "wpa_probe.h"
#include "aes.h"
#include "crypto.h"
static unsigned be16(const unsigned char *p) { return (unsigned)p[0]<<8|p[1]; }
static bool zero(const unsigned char *p,size_t n) { unsigned v=0;for (size_t i=0;i<n;++i) { v|=p[i]; }return !v; }
static int compare(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return (int)a[i]-(int)b[i]; } }return 0; }
static enum wpa_probe_result fail(struct wpa_probe *s,enum wpa_probe_result r) { s->failed=true;return r; }
void wpa_probe_clear(struct wpa_probe *s) { if (s) { crypto_wipe(s,sizeof(*s)); } }
enum wpa_probe_result wpa_probe_seed(struct wpa_probe *s,const unsigned char pmk[32],const unsigned char snonce[32])
{
    if (!s || !pmk || !snonce) { return WPA_PROBE_ARGUMENT; }if (s->seeded) { return WPA_PROBE_STATE; }
    if (zero(snonce,32)) { return fail(s,WPA_PROBE_NONCE); }
    for (unsigned i=0;i<32;++i) { s->pmk[i]=pmk[i];s->snonce[i]=snonce[i]; }s->seeded=true;return WPA_PROBE_OK;
}
enum wpa_probe_result wpa_probe_receive(struct wpa_probe *s,const void *buffer,size_t n)
{
    if (!s || !buffer) { return WPA_PROBE_ARGUMENT; }if (!s->seeded || s->failed) { return WPA_PROBE_STATE; }
    const unsigned char *e=buffer;if (n<99 || e[0]<1 || e[0]>3 || e[1]!=3 || be16(e+2)>n-4) { return fail(s,WPA_PROBE_FORMAT); }
    size_t body=be16(e+2);if (body<95 || body>616) { return fail(s,WPA_PROBE_FORMAT); }
    const unsigned char *k=e+4;unsigned info=be16(k+1);if (k[0]!=2 || be16(k+93)!=body-95) { return fail(s,WPA_PROBE_FORMAT); }
    if (info==0x88) {
        if (be16(k+3)!=16 || zero(k+13,32) || !zero(k+77,16)) { return fail(s,WPA_PROBE_FORMAT); }
        if (s->m1) { return crypto_equal(k+5,s->replay,8) && crypto_equal(k+13,s->anonce,32) && e[0]==s->version ? WPA_PROBE_OK : fail(s,WPA_PROBE_REPLAY); }
        if (crypto_equal(k+13,s->snonce,32)) { return fail(s,WPA_PROBE_NONCE); }
        for (unsigned i=0;i<8;++i) { s->replay[i]=k[5+i]; }for (unsigned i=0;i<32;++i) { s->anonce[i]=k[13+i]; }
        s->version=e[0];s->m1=true;return WPA_PROBE_OK;
    }
    if (!s->m2_issued || info!=0x13c8 || be16(k+3)!=16 || !zero(k+45,16) || !zero(k+69,8) || body-95<24 || (body-95)%8 || body-95>520 || e[0]!=s->version) { return fail(s,WPA_PROBE_FORMAT); }
    if (compare(k+5,s->replay,8)<=0) { s->retry_rejection=1;return fail(s,WPA_PROBE_REPLAY); }
    if (!crypto_equal(k+13,s->anonce,32)) { return fail(s,WPA_PROBE_NONCE); }
    if (s->m3_received) {
        /* RX only queues a candidate. Higher replay is not trusted until MIC,
         * unwrap and unchanged key-data/RSC checks run outside this callback. */
        if (s->retry_pending) {
            if (s->retry_bytes==body+4 && crypto_equal(e,s->retry,body+4)) {
                if (++s->m3_duplicates>8) { return fail(s,WPA_PROBE_LIMIT); }return WPA_PROBE_OK;
            }
            s->retry_rejection=4;return fail(s,WPA_PROBE_REPLAY); /* one bounded candidate slot */
        }
        if (s->m3_bytes==body+4 && crypto_equal(e,s->m3,body+4)) {
            if (++s->m3_duplicates>8) { return fail(s,WPA_PROBE_LIMIT); }
            if (s->validated_m3) { s->m4_due=true;s->m4_encoded=false; }return WPA_PROBE_OK;
        }
        int replay_order=compare(k+5,s->m3+9,8);
        if (replay_order<=0) { s->retry_rejection=replay_order<0 ? 2 : 3;return fail(s,WPA_PROBE_REPLAY); }
        if (++s->retries_queued>3) { return fail(s,WPA_PROBE_LIMIT); }
        s->retry_bytes=body+4;for (size_t i=0;i<s->retry_bytes;++i) { s->retry[i]=e[i]; }
        s->retry_pending=true;s->m4_encoded=false;return WPA_PROBE_OK;
    }
    s->m3_bytes=body+4;for (size_t i=0;i<s->m3_bytes;++i) { s->m3[i]=e[i]; }s->m3_received=true;return WPA_PROBE_OK;
}
enum wpa_probe_result wpa_probe_m2(struct wpa_probe *s,const unsigned char local[6],const unsigned char ap[6],const void *rsn,size_t n,const unsigned char pmkid[16],void *out,size_t cap,size_t *actual)
{
    if (actual) { *actual=0; }if (!s || !actual || !local || !ap || !rsn || !pmkid || !out) { return WPA_PROBE_ARGUMENT; }
    if (!s->seeded || !s->m1 || s->m2_issued || s->failed) { return WPA_PROBE_STATE; }
    struct wpa_m2_input in={.version=s->version,.rsn=rsn,.rsn_bytes=n};
    bool ok=wpa_sae_ptk(s->pmk,local,ap,s->snonce,s->anonce,&s->ptk);crypto_wipe(s->pmk,32);
    for (unsigned i=0;i<6;++i) { in.local[i]=local[i];in.ap[i]=ap[i]; }for (unsigned i=0;i<16;++i) { in.kck[i]=s->ptk.kck[i];in.pmkid[i]=pmkid[i]; }
    for (unsigned i=0;i<32;++i) { in.snonce[i]=s->snonce[i]; }for (unsigned i=0;i<8;++i) { in.replay[i]=s->replay[i]; }
    ok=ok && wpa_sae_m2(&in,out,cap,actual);crypto_wipe(&in,sizeof(in));if (!ok) { return fail(s,WPA_PROBE_FORMAT); }
    for (unsigned i=0;i<6;++i) { s->local[i]=local[i];s->peer[i]=ap[i]; }s->m2_issued=true;return WPA_PROBE_OK;
}
static enum wpa_probe_result verify_frame(struct wpa_probe *s)
{
    bool retry=s->validated_m3;
    unsigned char saved[16],mic[16],plain[512];for (unsigned i=0;i<16;++i) { saved[i]=s->m3[81+i];s->m3[81+i]=0; }
    bool ok=aes128_cmac(s->ptk.kck,s->m3,s->m3_bytes,mic) && crypto_equal(saved,mic,16);
    for (unsigned i=0;i<16;++i) { s->m3[81+i]=saved[i]; }crypto_wipe(saved,16);crypto_wipe(mic,16);
    if (!ok) { crypto_wipe(plain,sizeof(plain));return fail(s,WPA_PROBE_MIC); }s->mic_verified=true;
    size_t n=0;ok=aes128_unwrap(s->ptk.kek,s->m3+99,s->m3_bytes-99,plain,sizeof(plain),&n);
    if (!ok) { crypto_wipe(plain,sizeof(plain));return fail(s,WPA_PROBE_UNWRAP); }s->plain_bytes=n;s->unwrap_verified=true;
    unsigned char digest[32];struct sha256 hash;sha256_init(&hash);sha256_update(&hash,plain,n);sha256_final(&hash,digest);crypto_wipe(&hash,sizeof(hash));
    if (retry) {
        /* A retry must never change any GTK/IGTK, counter, RSN or padding.
         * Do not republish or reinstall keys, even after valid MIC/unwrap. */
        ok=crypto_equal(digest,s->key_data_digest,32) && crypto_equal(s->m3+65,s->initial_rsc,8);
        crypto_wipe(digest,sizeof(digest));crypto_wipe(plain,sizeof(plain));
        if (!ok) { return fail(s,WPA_PROBE_CHANGED); }
        ++s->retries_verified;s->m4_due=true;s->m4_encoded=false;return WPA_PROBE_OK;
    }
    for (unsigned i=0;i<32;++i) { s->key_data_digest[i]=digest[i]; }crypto_wipe(digest,sizeof(digest));
    for (unsigned i=0;i<8;++i) { s->initial_rsc[i]=s->m3[65+i]; }
    if (s->key_data_required) {
        struct wpa_group_keys keys={0};
        s->key_data_result=wpa_sae_key_data(s->ap_rsn,s->ap_rsn_bytes,plain,n,s->m3+65,&s->key_data,&keys);
        if (s->key_data_result==KDE_OK && s->retain_group) { s->group=keys;s->group_retained=true; }
        crypto_wipe(&keys,sizeof(keys));crypto_wipe(plain,sizeof(plain));
        if (s->key_data_result!=KDE_OK) { return fail(s,WPA_PROBE_KEY_DATA); }s->key_data_verified=true;
    }
    s->validated_m3=true;s->m4_due=true;crypto_wipe(plain,sizeof(plain));return WPA_PROBE_OK;
}
enum wpa_probe_result wpa_probe_verify(struct wpa_probe *s)
{
    if (!s) { return WPA_PROBE_ARGUMENT; }if (s->failed || !s->m2_written) { return WPA_PROBE_STATE; }if (!s->m3_received) { return WPA_PROBE_WAIT; }
    enum wpa_probe_result r=WPA_PROBE_OK;
    if (!s->validated_m3) { r=verify_frame(s);if (r!=WPA_PROBE_OK) { return r; } }
    if (!s->retry_pending) { return WPA_PROBE_OK; }
    s->m3_bytes=s->retry_bytes;for (size_t i=0;i<s->m3_bytes;++i) { s->m3[i]=s->retry[i]; }
    crypto_wipe(s->retry,sizeof(s->retry));s->retry_bytes=0;s->retry_pending=false;
    return verify_frame(s);
}
enum wpa_probe_result wpa_probe_bind_ap(struct wpa_probe *s,const void *rsn,size_t n)
{
    if (!s || !rsn) { return WPA_PROBE_ARGUMENT; }if (!s->seeded || s->failed || s->m1 || s->key_data_required) { return WPA_PROBE_STATE; }
    s->key_data_result=wpa_ap_rsn_profile(rsn,n) ? KDE_OK : KDE_AP_PROFILE;
    if (s->key_data_result!=KDE_OK) { return fail(s,WPA_PROBE_KEY_DATA); }
    for (size_t i=0;i<n;++i) { s->ap_rsn[i]=((const unsigned char *)rsn)[i]; }s->ap_rsn_bytes=n;s->key_data_required=true;return WPA_PROBE_OK;
}
enum wpa_probe_result wpa_probe_m4(struct wpa_probe *s,const unsigned char local[6],const unsigned char ap[6],void *out,size_t cap,size_t *actual)
{
    if (actual) { *actual=0; }if (!s || !local || !ap || !out || !actual) { return WPA_PROBE_ARGUMENT; }
    if (s->failed || !s->m2_written || !s->validated_m3 || !s->mic_verified || !s->unwrap_verified || !s->key_data_verified || !s->m4_due || s->retry_pending) { return WPA_PROBE_STATE; }
    if (!crypto_equal(local,s->local,6) || !crypto_equal(ap,s->peer,6)) { return fail(s,WPA_PROBE_FORMAT); }
    struct wpa_m4_input in={.version=s->version};for (unsigned i=0;i<6;++i) { in.local[i]=local[i];in.ap[i]=ap[i]; }
    for (unsigned i=0;i<16;++i) { in.kck[i]=s->ptk.kck[i]; }for (unsigned i=0;i<8;++i) { in.replay[i]=s->m3[9+i]; }
    bool ok=wpa_sae_m4(&in,out,cap,actual);crypto_wipe(&in,sizeof(in));if (!ok) { return fail(s,WPA_PROBE_FORMAT); }
    s->m4_encoded=true;s->m4_due=false;s->m4_bytes=*actual;return WPA_PROBE_OK;
}
const char *wpa_probe_error(enum wpa_probe_result r)
{
    static const char *const names[]={"ok","waiting for M3","argument","state","EAPOL format/profile","replay/mismatched retry","nonce","M3 MIC","key unwrap integrity","authenticated key data","M3 retry changed key data/counters","M3 retry limit"};
    return (unsigned)r<sizeof(names)/sizeof(names[0]) ? names[r] : "unknown";
}
