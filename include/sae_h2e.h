#ifndef OS_SAE_H2E_H
#define OS_SAE_H2E_H
#include <stdbool.h>
#include <stddef.h>
/* Experimental SAE group 19 only. Bounds: SSID 1..32 bytes, password 1..128,
 * optional identifier 0..64. Fixed 64-byte X||Y points. Inputs must not overlap
 * output; failures erase output. These helpers do not store credentials, send
 * frames, or implement SAE commit/confirm, association or a key handshake. */
bool sae_h2e_pt(const void *ssid,size_t ssid_bytes,const void *password,size_t password_bytes,const void *identifier,size_t identifier_bytes,unsigned char output[64]);
bool sae_h2e_pwe(const unsigned char pt[64],const unsigned char mac_a[6],const unsigned char mac_b[6],unsigned char output[64]);
bool sae_h2e_selftest(void);
#endif
