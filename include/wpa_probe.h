#ifndef OS_WPA_PROBE_H
#define OS_WPA_PROBE_H
#include "wpa_keys.h"
#include "wpa_kde.h"
#include <stdint.h>
enum wpa_probe_result { WPA_PROBE_OK,WPA_PROBE_WAIT,WPA_PROBE_ARGUMENT,WPA_PROBE_STATE,WPA_PROBE_FORMAT,WPA_PROBE_REPLAY,WPA_PROBE_NONCE,WPA_PROBE_MIC,WPA_PROBE_UNWRAP,WPA_PROBE_KEY_DATA,WPA_PROBE_CHANGED,WPA_PROBE_LIMIT };
/* Private synchronous working state, never place in console diagnostics.
 * Receive only parses/copies. Prepare/verify run outside receive callbacks.
 * Optional authenticated KDE validation + offline M4. No M4 TX/key install. */
struct wpa_probe {
    bool seeded,m1,m2_issued,m2_written,m3_received,mic_verified,unwrap_verified,failed;
    unsigned version;size_t m3_bytes,plain_bytes;
    unsigned char pmk[32],snonce[32],anonce[32],replay[8];struct wpa_sae_ptk ptk;
    unsigned char m3[620];
    bool key_data_required,key_data_verified,m4_encoded;size_t ap_rsn_bytes,m4_bytes;
    unsigned char ap_rsn[257];struct wpa_kde_info key_data;enum wpa_kde_result key_data_result;
    unsigned char local[6],peer[6];
    bool retain_group,group_retained;struct wpa_group_keys group; /* private */
    bool validated_m3,retry_pending,m4_due;unsigned m3_duplicates,retries_queued,retries_verified;
    unsigned retry_rejection; /* 1<=M1, 2<latest, 3=same-counter change, 4=busy slot */
    size_t retry_bytes;unsigned char retry[620],key_data_digest[32],initial_rsc[8];
};
enum wpa_probe_result wpa_probe_seed(struct wpa_probe *,const unsigned char pmk[32],const unsigned char snonce[32]);
enum wpa_probe_result wpa_probe_receive(struct wpa_probe *,const void *eapol,size_t);
enum wpa_probe_result wpa_probe_m2(struct wpa_probe *,const unsigned char local[6],const unsigned char ap[6],const void *rsn,size_t,const unsigned char pmkid[16],void *,size_t,size_t *);
enum wpa_probe_result wpa_probe_verify(struct wpa_probe *);
enum wpa_probe_result wpa_probe_bind_ap(struct wpa_probe *,const void *,size_t);
enum wpa_probe_result wpa_probe_m4(struct wpa_probe *,const unsigned char local[6],const unsigned char ap[6],void *,size_t,size_t *);
void wpa_probe_clear(struct wpa_probe *);
const char *wpa_probe_error(enum wpa_probe_result);
bool wpa_retry_selftest(void);
#endif
