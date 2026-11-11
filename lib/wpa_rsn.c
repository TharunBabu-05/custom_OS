#include "wpa_rsn.h"
#include "crypto.h"
static unsigned le16(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8; }
static bool suite(const unsigned char *p,unsigned type) { return !p[0] && p[1]==15 && p[2]==172 && p[3]==type; }
enum wpa_rsn_result wpa_sae_rsn(const void *buffer,size_t n,const unsigned char pmkid[16],struct wpa_rsn_info *out)
{
    if (!out) { return RSN_ARGUMENT; }*out=(struct wpa_rsn_info){0};
    if (!buffer || !pmkid) { return RSN_ARGUMENT; }const unsigned char *p=buffer;
    if (n<22 || n>257 || p[0]!=48 || (size_t)p[1]+2!=n) { return RSN_LENGTH; }
    if (le16(p+2)!=1) { return RSN_VERSION; }if (!suite(p+4,4)) { return RSN_GROUP; }
    if (le16(p+8)!=1 || !suite(p+10,4)) { return RSN_PAIRWISE; }
    if (le16(p+14)!=1 || !suite(p+16,8)) { return RSN_AKM; }
    out->capabilities=(uint16_t)le16(p+20);
    if ((out->capabilities&0xc0)!=0xc0) { return RSN_PMF; }
    if (out->capabilities&~0xfcu) { return RSN_CAPABILITIES; }
    if (n==22) { return RSN_OK; }if (n<24) { return RSN_LENGTH; }
    out->pmkid_count=(uint16_t)le16(p+22);if (out->pmkid_count>1) { return RSN_PMKID; }
    size_t at=24+16u*out->pmkid_count;if (at>n) { return RSN_LENGTH; }
    if (out->pmkid_count && !crypto_equal(p+24,pmkid,16)) { return RSN_PMKID; }
    if (at==n) { return RSN_OK; }if (n-at!=4) { return RSN_LENGTH; }
    out->management_explicit=true;return suite(p+at,6) ? RSN_OK : RSN_MANAGEMENT;
}
const char *wpa_rsn_error(enum wpa_rsn_result r)
{
    static const char *const names[]={"ok","argument","length","version","group cipher","pairwise cipher/count","AKM/count","required PMF","unsupported capabilities","PMKID/count","management cipher"};
    return (unsigned)r<sizeof(names)/sizeof(names[0]) ? names[r] : "unknown";
}
