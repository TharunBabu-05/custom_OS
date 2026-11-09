#ifndef OS_SHA256_H
#define OS_SHA256_H
#include <stddef.h>
#include <stdint.h>
struct sha256 { uint32_t h[8]; uint64_t bytes; unsigned used; unsigned char block[64]; };
void sha256_init(struct sha256 *state);
void sha256_update(struct sha256 *state,const void *data,size_t length);
void sha256_final(struct sha256 *state,unsigned char digest[32]);
#endif
