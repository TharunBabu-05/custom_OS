#ifndef OS_WPA_RSN_H
#define OS_WPA_RSN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum wpa_rsn_result { RSN_OK,RSN_ARGUMENT,RSN_LENGTH,RSN_VERSION,RSN_GROUP,RSN_PAIRWISE,RSN_AKM,RSN_PMF,RSN_CAPABILITIES,RSN_PMKID,RSN_MANAGEMENT };
struct wpa_rsn_info { uint16_t capabilities,pmkid_count;bool management_explicit; };
/* Single SAE AKM-8/CCMP-128 with required PMF. Omitted management cipher
 * means the specified BIP-CMAC-128 default. Optional PMKID must match. */
enum wpa_rsn_result wpa_sae_rsn(const void *,size_t,const unsigned char pmkid[16],struct wpa_rsn_info *);
const char *wpa_rsn_error(enum wpa_rsn_result);
#endif
