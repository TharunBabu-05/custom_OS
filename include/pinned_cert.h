#ifndef OS_PINNED_CERT_H
#define OS_PINNED_CERT_H
#include <stddef.h>
#include <stdint.h>

enum { PINNED_CERT_MAX=2048 };
enum cert_result { CERT_OK,CERT_FORMAT,CERT_NAME,CERT_SIGNATURE };
struct pinned_cert {
    unsigned char digest[32],key[64];
    uint64_t not_before,not_after;
};
/* Exact self-signed P-256/SHA-256 leaf for a DNS name. Check its pin and
 * validity with the trust policy before using the returned public key. */
enum cert_result pinned_cert_verify(const void *,size_t,const char *,struct pinned_cert *);
#endif
