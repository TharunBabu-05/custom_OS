#ifndef OS_TLS13_AUTH_H
#define OS_TLS13_AUTH_H
#include <stdbool.h>
#include <stddef.h>
#include "pinned_cert.h"

enum { TLS13_TRANSCRIPT_MAX=4096 };
enum tls13_auth_result {
    TLS13_AUTH_OK,
    TLS13_AUTH_FORMAT,
    TLS13_AUTH_CERTIFICATE,
    TLS13_AUTH_NAME,
    TLS13_AUTH_TRUST,
    TLS13_AUTH_SIGNATURE,
    TLS13_AUTH_FINISHED
};
typedef bool (*tls13_trust_fn)(const struct pinned_cert *,void *);
/* Validate the two complete plaintext hello messages and return the
 * server's uncompressed P-256 coordinates within the input buffer. */
const unsigned char *tls13_server_share(const void *hellos,size_t bytes);

/* Authenticate a bounded plaintext handshake through server Finished. The
 * caller supplies the handshake traffic secret for this connection and the
 * leaf trust policy. The profile uses one pinned P-256 leaf and SHA-256. */
enum tls13_auth_result tls13_authenticate(const void *transcript,size_t bytes,const unsigned char secret[32],
    const char *name,tls13_trust_fn trust,void *context);
#endif
