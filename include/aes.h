#ifndef OS_AES_H
#define OS_AES_H
#include <stdbool.h>
#include <stddef.h>
/* Original AES-128 primitives. Fixed GF(256) inversion replaces secret-indexed
 * S-box tables. Source-level discipline is not an independent timing audit.
 * Block operations permit in-place input/output. CMAC permits message/output
 * overlap. All temporary expanded keys are erased. */
bool aes128_encrypt(const unsigned char key[16],const unsigned char input[16],unsigned char output[16]);
bool aes128_decrypt(const unsigned char key[16],const unsigned char input[16],unsigned char output[16]);
bool aes128_cmac(const unsigned char key[16],const void *,size_t,unsigned char output[16]);
/* RFC3394 integrity-checked unwrap, 2..64 blocks, no RFC5649 padding.
 * Output may not overlap inputs. Bounded provided output is erased on failure. */
bool aes128_unwrap(const unsigned char key[16],const void *wrapped,size_t length,void *output,size_t capacity,size_t *actual);
bool aes128_selftest(void);
#endif
