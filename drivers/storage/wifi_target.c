/* Original narrow WPA3 advertisement preflight; not a supplicant. */
#include "wifi_target.h"
static bool equal(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }return true; }
enum wifi_sae_method wifi_target_sae_method(const struct brcm_scan_ap *ap)
{
    if (!ap || !ap->akm_sae || ap->rsnxe_length>sizeof(ap->rsnxe)) { return WIFI_SAE_METHOD_INVALID; }
    if (!ap->rsnxe_length) { return WIFI_SAE_HUNT; }
    if (ap->rsnxe_length!=(unsigned)(ap->rsnxe[0]&15)+1) { return WIFI_SAE_METHOD_INVALID; }
    return ap->rsnxe[0]&32 ? WIFI_SAE_H2E : WIFI_SAE_HUNT;
}
enum wifi_target_result wifi_target_check(const struct brcm_scan_state *scan,const struct brcm_caps_state *caps,const void *ssid,size_t bytes,const struct brcm_scan_ap **target)
{
    if (!target) { return WIFI_TARGET_ARGUMENT; }*target=NULL;
    if (!scan || !caps || !ssid || !bytes || bytes>32) { return WIFI_TARGET_ARGUMENT; }
    if (!scan->ready || scan->error!=BLOCK_OK || !scan->interface_down || !scan->mask_restored || scan->interface_may_be_up || scan->truncated || scan->malformed || scan->count>BRCM_SCAN_AP_MAX) { return WIFI_TARGET_SCAN; }
    const struct brcm_scan_ap *ap=NULL;
    for (unsigned i=0;i<scan->count;++i) {
        if (scan->ap[i].ssid_length==bytes && equal(scan->ap[i].ssid,ssid,bytes)) {
            if (ap) { return WIFI_TARGET_AMBIGUOUS; }ap=&scan->ap[i];
        }
    }
    if (!ap) { return WIFI_TARGET_NOT_FOUND; }*target=ap;
    if (!(ap->security&SCAN_PRIVACY) || !(ap->security&SCAN_RSN)) { return WIFI_TARGET_PRIVACY; }
    if (!ap->akm_sae) { return WIFI_TARGET_AKM; }
    if (!ap->pairwise_ccmp || ap->group_cipher!=0x000fac04) { return WIFI_TARGET_CIPHER; }
    if (!ap->pmf_capable || (ap->pmf_required && !ap->pmf_capable)) { return WIFI_TARGET_PMF; }
    if (ap->management_cipher!=0x000fac06) { return WIFI_TARGET_MANAGEMENT; }
    if (!caps->ready || caps->error!=BLOCK_OK || !caps->external_sae || !caps->mfp_known || !caps->mfp_supported) { return WIFI_TARGET_FIRMWARE; }
    return WIFI_TARGET_OK;
}
const char *wifi_target_error(enum wifi_target_result r)
{
    static const char *const messages[]={
        "advertisement matches SAE/CCMP-128/BIP-CMAC-128 with PMF",
        "SSID must contain 1 to 32 bytes",
        "a complete clean scan with interface DOWN and restored event mask is required",
        "SSID not found in cached scan",
        "multiple BSSIDs advertise this SSID; explicit BSSID selection is required",
        "RSN and privacy advertisements are required",
        "ordinary SAE AKM 00:0f:ac:8 is absent; no PSK/FT-SAE fallback",
        "CCMP-128 pairwise and group advertisements are required",
        "PMF capability is required",
        "BIP-CMAC-128 management cipher is required",
        "verified firmware external-SAE and PMF capabilities are required"
    };
    return (unsigned)r<sizeof(messages)/sizeof(messages[0]) ? messages[r] : "invalid target result";
}
