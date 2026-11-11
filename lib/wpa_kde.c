/* Original bounded initial SAE/CCMP/PMF M3 key-data parser. Wire definitions:
 * hostap wpa_common.h / rsn_supp/wpa.c; see docs/wifi-key-data.md. */
#include "wpa_kde.h"
#include "crypto.h"
static unsigned le16(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8; }
static bool zero(const unsigned char *p,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=p[i]; }return !x; }
static bool suite(const unsigned char *p,unsigned type) { return !p[0] && p[1]==15 && p[2]==172 && p[3]==type; }
static bool overlap(const void *a,size_t n,const void *b,size_t m)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && n && m && (x<=y ? y-x<n : x-y<m); }
bool wpa_ap_rsn_profile(const void *buffer,size_t n)
{
    const unsigned char *p=buffer;if (!p || n<22 || n>257 || p[0]!=48 || (size_t)p[1]+2!=n || le16(p+2)!=1 || !suite(p+4,4) || le16(p+8)!=1 || !suite(p+10,4)) { return false; }
    unsigned count=le16(p+14);if (count<1 || count>2 || n<18+4u*count) { return false; }
    bool sae=false,psk=false;for (unsigned i=0;i<count;++i) {
        const unsigned char *akm=p+16+4*i;
        if (suite(akm,8) && !sae) { sae=true; }else if (suite(akm,2) && !psk) { psk=true; }else { return false; }
    }
    size_t at=16+4u*count;unsigned caps=le16(p+at);at+=2;
    if (!sae || !(caps&128) || (caps&~0xfcu)) { return false; }
    if (at==n) { return true; }if (n-at<2 || le16(p+at)) { return false; }at+=2;
    return at==n || (n-at==4 && suite(p+at,6));
}
enum wpa_kde_result wpa_sae_key_data(const void *ap_rsn,size_t rn,const void *buffer,size_t n,const unsigned char rsc[8],struct wpa_kde_info *diag,struct wpa_group_keys *out)
{
    if (!diag || !out) { if (out) { crypto_wipe(out,sizeof(*out)); }return KDE_ARGUMENT; }
    /* Validate aliases before clearing either output. No partial key publish. */
    if (overlap(out,sizeof(*out),diag,sizeof(*diag)) || overlap(out,sizeof(*out),ap_rsn,rn) || overlap(out,sizeof(*out),buffer,n) || overlap(out,sizeof(*out),rsc,8) ||
        overlap(diag,sizeof(*diag),ap_rsn,rn) || overlap(diag,sizeof(*diag),buffer,n) || overlap(diag,sizeof(*diag),rsc,8)) { crypto_wipe(out,sizeof(*out));return KDE_ARGUMENT; }
    *diag=(struct wpa_kde_info){0};crypto_wipe(out,sizeof(*out));
    if (!buffer || !ap_rsn || !rsc) { return KDE_ARGUMENT; }
    if (!wpa_ap_rsn_profile(ap_rsn,rn)) { return KDE_AP_PROFILE; }
    if (n<16 || n>512 || n%8) { return KDE_LENGTH; }if (rsc[6] || rsc[7]) { return KDE_RSC; }
    const unsigned char *p=buffer;struct wpa_group_keys keys={0};enum wpa_kde_result result=KDE_OK;
    for (size_t at=0;at<n;) {
        size_t left=n-at;diag->error_offset=at;diag->tag=p[at]==48 ? 48 : p[at]==221 ? 221 : 0;diag->selector=0;
        /* AES-wrap padding is dd followed by zero or more zeros, <=7 bytes.
         * An arbitrary zero tail is not a sequence of valid empty IEs. */
        if (p[at]==221 && left<=7 && zero(p+at+1,left-1)) { diag->padding_bytes=(unsigned)left;at=n;break; }
        if (left<2 || (size_t)p[at+1]+2>left) { result=KDE_LENGTH;break; }
        size_t bytes=2u+p[at+1];const unsigned char *ie=p+at;
        if (ie[0]==48) {
            if (diag->rsn_match) { result=KDE_DUPLICATE;break; }
            diag->rsn_bytes=(unsigned)bytes;
            if (bytes!=rn || !crypto_equal(ie,ap_rsn,rn)) { result=KDE_RSN_MISMATCH;break; }diag->rsn_match=true;
        }else if (ie[0]==221) {
            if (bytes<6) { result=ie[1] ? KDE_LENGTH : KDE_PADDING;break; }
            diag->selector=ie[5]==1 ? 1 : ie[5]==9 ? 9 : 0;if (ie[2] || ie[3]!=15 || ie[4]!=172) { result=KDE_UNSUPPORTED;break; }
            if (ie[5]==1) {
                if (diag->gtk_seen) { result=KDE_DUPLICATE;break; }
                if (bytes!=24 || (ie[6]&0xf8) || ie[7]) { result=KDE_GTK;break; }
                keys.gtk_id=ie[6]&3;keys.gtk_tx=!!(ie[6]&4);diag->gtk_seen=true;diag->gtk_id=keys.gtk_id;diag->gtk_bytes=16;
                for (unsigned i=0;i<16;++i) { keys.gtk[i]=ie[8+i]; }
            }else if (ie[5]==9) {
                if (diag->igtk_seen) { result=KDE_DUPLICATE;break; }
                if (bytes!=30 || (le16(ie+6)!=4 && le16(ie+6)!=5)) { result=KDE_IGTK;break; }
                keys.igtk_id=le16(ie+6);diag->igtk_seen=true;diag->igtk_id=keys.igtk_id;diag->igtk_bytes=16;
                for (unsigned i=0;i<6;++i) { keys.ipn[i]=ie[8+i]; }for (unsigned i=0;i<16;++i) { keys.igtk[i]=ie[14+i]; }
            }else { result=KDE_UNSUPPORTED;break; }
        }else { result=KDE_UNSUPPORTED;break; }
        at+=bytes;
    }
    if (result==KDE_OK && (!diag->rsn_match || !diag->gtk_seen || !diag->igtk_seen)) { result=KDE_MISSING; }
    if (result==KDE_OK) { for (unsigned i=0;i<6;++i) { keys.rsc[i]=rsc[i]; }*out=keys; }
    crypto_wipe(&keys,sizeof(keys));return result;
}
const char *wpa_kde_error(enum wpa_kde_result r)
{
    static const char *const names[]={"ok","argument/alias","length","AP RSN profile","AP RSN differs from scan","duplicate element","GTK format","IGTK format/ID","unsupported element/selector","missing RSN/GTK/IGTK","padding","CCMP receive counter"};
    return (unsigned)r<sizeof(names)/sizeof(names[0]) ? names[r] : "unknown";
}
