#ifndef OS_P256_H
#define OS_P256_H
#include <stdbool.h>
/* Experimental SAE group-19 arithmetic. Fixed-width big-endian encodings:
 * scalar: 32 bytes, 1 <= scalar < n; point: 64 bytes X || Y, no SEC1 prefix.
 * Reject noncanonical/off-curve points and infinity. Failed operations erase
 * their 64-byte output; output may overlap inputs. No credentials or radio I/O.
 * Not yet integrated with authentication or independently side-channel audited. */
void p256_generator(unsigned char point[64]);
bool p256_scalar_valid(const unsigned char scalar[32]);
bool p256_point_valid(const unsigned char point[64]);
bool p256_multiply(const unsigned char scalar[32],const unsigned char point[64],unsigned char output[64]);
bool p256_add(const unsigned char a[64],const unsigned char b[64],unsigned char output[64]);
bool p256_negate(const unsigned char point[64],unsigned char output[64]);
/* SAE group-19 SSWU (Z=-10): reduce a 48-byte big-endian value modulo p,
 * map it to the curve, select Y parity from the reduced field element. */
bool p256_sswu(const unsigned char value[48],unsigned char output[64]);
/* MAC-binding hash reduction, (hash mod (n-1))+1. This is not an RNG. */
bool p256_scalar_from_hash(const unsigned char hash[32],unsigned char output[32]);
/* Add two scalars in 1..n-1 modulo n. A zero result is valid here (SAE KDF
 * context); callers must impose the separate >1 commit-scalar constraint. */
bool p256_scalar_sum(const unsigned char a[32],const unsigned char b[32],unsigned char output[32]);
bool p256_selftest(void);
#endif
