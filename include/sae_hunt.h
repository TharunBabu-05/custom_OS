#ifndef OS_SAE_HUNT_H
#define OS_SAE_HUNT_H
#include <stdbool.h>
#include <stddef.h>
/* Original SAE group-19 hunting-and-pecking PWE derivation. No SSID salt,
 * identifier, token, radio I/O or credentials retained. Exactly 40 candidates
 * are evaluated; first valid point is selected with byte masks. No early exit
 * after finding a point. This source-level discipline is not an independent
 * compiler/CPU side-channel audit. Output must not overlap any input.
 * Password: 1..128 bytes, two distinct nonzero unicast MACs. */
bool sae_hunt_pwe(const void *password,size_t length,const unsigned char a[6],const unsigned char b[6],unsigned char output[64]);
#endif
