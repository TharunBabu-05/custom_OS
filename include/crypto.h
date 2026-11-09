#ifndef OS_CRYPTO_H
#define OS_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include "sha256.h"
struct crypto_span { const void *data;size_t length; };
void crypto_wipe(void *,size_t);
bool crypto_equal(const void *,const void *,size_t);
bool hmac_sha256(const void *key,size_t key_bytes,const struct crypto_span *,size_t spans,unsigned char output[32]);
/* RFC 5869 SHA256 HKDF; inputs and output must not overlap. Maximum output
 * 255*32 bytes. Neither primitive supplies entropy or implements SAE. */
bool hkdf_sha256(const void *salt,size_t salt_bytes,const void *ikm,size_t ikm_bytes,const void *info,size_t info_bytes,void *output,size_t output_bytes);
bool crypto_selftest(void);
#endif
