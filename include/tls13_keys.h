#ifndef OS_TLS13_KEYS_H
#define OS_TLS13_KEYS_H
#include <stdbool.h>
#include <stddef.h>

struct tls13_key_schedule { unsigned char client[32],server[32],master[32]; };
void tls13_hash(const void *,size_t,unsigned char digest[32]);
bool tls13_expand(const unsigned char secret[32],const char *label,
    const void *context,size_t context_bytes,void *out,size_t bytes);
bool tls13_finished(const unsigned char secret[32],const unsigned char digest[32],unsigned char out[32]);
bool tls13_handshake_keys(const unsigned char shared[32],const unsigned char hello_hash[32],struct tls13_key_schedule *);
#endif
