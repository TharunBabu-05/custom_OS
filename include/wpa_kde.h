#ifndef OS_WPA_KDE_H
#define OS_WPA_KDE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum wpa_kde_result { KDE_OK,KDE_ARGUMENT,KDE_LENGTH,KDE_AP_PROFILE,KDE_RSN_MISMATCH,KDE_DUPLICATE,KDE_GTK,KDE_IGTK,KDE_UNSUPPORTED,KDE_MISSING,KDE_PADDING,KDE_RSC };
/* Public diagnostics only. Never place GTK/IGTK or packet numbers here. */
struct wpa_kde_info {
    bool rsn_match,gtk_seen,igtk_seen;unsigned gtk_id,igtk_id,rsn_bytes,gtk_bytes,igtk_bytes,padding_bytes;
    size_t error_offset;unsigned tag,selector;
};
/* Private extraction result. Caller must erase it; output is zero on failure.
 * No installation, packet transmission or port authorization in this API. */
struct wpa_group_keys { unsigned char gtk[16],igtk[16],rsc[6],ipn[6];unsigned gtk_id,igtk_id;bool gtk_tx; };
/* Narrow AP profile: CCMP-128, SAE optionally alongside PSK, PMF capable,
 * BIP-CMAC-128; no FT, TKIP, extended AKMs or PMKID list. */
bool wpa_ap_rsn_profile(const void *,size_t);
/* Only call after authenticated M3 MIC + unwrap. Bind the AP RSN byte-for-byte
 * to the selected scan result, not to the station's SAE-only request RSN. */
enum wpa_kde_result wpa_sae_key_data(const void *ap_rsn,size_t,const void *plain,size_t,const unsigned char rsc[8],struct wpa_kde_info *,struct wpa_group_keys *);
const char *wpa_kde_error(enum wpa_kde_result);
bool wpa_kde_selftest(void);
#endif
