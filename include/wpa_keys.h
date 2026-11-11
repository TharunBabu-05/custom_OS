#ifndef OS_WPA_KEYS_H
#define OS_WPA_KEYS_H
#include <stdbool.h>
#include <stddef.h>
struct wpa_sae_ptk { unsigned char kck[16],kek[16],tk[16]; };
/* Ordinary SAE AKM-8 + CCMP-128: IEEE SHA256 PRF, 384 output bits.
 * No state, RNG, transmission or key install. Inputs/output must not overlap. */
bool wpa_sae_ptk(const unsigned char pmk[32],const unsigned char local[6],const unsigned char ap[6],const unsigned char snonce[32],const unsigned char anonce[32],struct wpa_sae_ptk *);
struct wpa_m2_input {
    unsigned char local[6],ap[6],kck[16],snonce[32],replay[8],pmkid[16];unsigned version;
    const void *rsn;size_t rsn_bytes;
};
/* Pure Ethernet/EAPOL M2 serialization with AKM-defined version 0 and
 * AES-CMAC MIC. The RSN is validated but must be supplied from a separately
 * verified association. This function cannot authorize or send anything. */
bool wpa_sae_m2(const struct wpa_m2_input *,void *output,size_t capacity,size_t *actual);
struct wpa_m4_input { unsigned char local[6],ap[6],kck[16],replay[8];unsigned version; };
/* Pure initial SAE M4 encoder: secure+pairwise+MIC, empty nonce/key-data,
 * replay from authenticated M3. It does not send, install keys or authorize. */
bool wpa_sae_m4(const struct wpa_m4_input *,void *,size_t,size_t *);
bool wpa_keys_selftest(void);
#endif
