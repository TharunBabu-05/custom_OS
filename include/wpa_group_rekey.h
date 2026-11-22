#ifndef OS_WPA_GROUP_REKEY_H
#define OS_WPA_GROUP_REKEY_H
#include "wpa_keys.h"
#include "wpa_kde.h"

#define WPA_GROUP_FRAME_MAX (113+520)
enum wpa_group_result { WPA_GROUP_OK,WPA_GROUP_ARGUMENT,WPA_GROUP_FORMAT,WPA_GROUP_REPLAY,
    WPA_GROUP_MIC,WPA_GROUP_UNWRAP,WPA_GROUP_KEY_DATA };
struct wpa_group_context {
    unsigned char local[6],peer[6],kck[16],kek[16],last_replay[8];
    unsigned version;
};
struct wpa_group_offer {
    struct wpa_group_keys keys;
    unsigned char replay[8],reply[113];
};
/* Offline only. Validate a complete Ethernet Group Key message 1 against a
 * verified SAE/CCMP/PMF context. No replay advancement, I/O or key install.
 * Inputs must not overlap offer. Caller must erase a successful offer. */
enum wpa_group_result wpa_group_validate(const struct wpa_group_context *,const void *,size_t,struct wpa_group_offer *);
#endif
