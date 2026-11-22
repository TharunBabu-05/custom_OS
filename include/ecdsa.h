#ifndef OS_ECDSA_H
#define OS_ECDSA_H
#include <stdbool.h>
#include <stddef.h>
/* P-256 ECDSA verification only. All inputs are public, big-endian values. */
bool ecdsa_p256_verify(const unsigned char key[64],const unsigned char hash[32],const unsigned char r[32],const unsigned char s[32]);
/* Decode the strictly encoded DER SEQUENCE of two positive P-256 scalars. */
bool ecdsa_p256_decode_der(const void *,size_t,unsigned char r[32],unsigned char s[32]);
#endif
