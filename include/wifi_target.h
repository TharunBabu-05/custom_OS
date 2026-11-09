#ifndef OS_WIFI_TARGET_H
#define OS_WIFI_TARGET_H
#include "brcm_scan.h"
enum wifi_target_result {
    WIFI_TARGET_OK,WIFI_TARGET_ARGUMENT,WIFI_TARGET_SCAN,WIFI_TARGET_NOT_FOUND,
    WIFI_TARGET_AMBIGUOUS,WIFI_TARGET_PRIVACY,WIFI_TARGET_AKM,WIFI_TARGET_CIPHER,
    WIFI_TARGET_PMF,WIFI_TARGET_MANAGEMENT,WIFI_TARGET_FIRMWARE
};
/* Cached advertisement checks only. No I/O, credentials, keys or association.
 * An SSID with multiple BSSIDs is deliberately refused until explicit selection exists. */
enum wifi_target_result wifi_target_check(const struct brcm_scan_state *,const struct brcm_caps_state *,const void *,size_t,const struct brcm_scan_ap **);
const char *wifi_target_error(enum wifi_target_result);
#endif
