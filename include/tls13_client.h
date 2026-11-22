#ifndef OS_TLS13_CLIENT_H
#define OS_TLS13_CLIENT_H
#include "tls13_auth.h"

enum { TLS13_ECHO_MAX=256 };
struct tls13_io {
    void *context;
    bool (*read_exact)(void *,void *,size_t);
    bool (*write_all)(void *,const void *,size_t);
    bool (*random)(void *,void *,size_t);
};
enum tls13_client_result {
    TLS13_CLIENT_OK,
    TLS13_CLIENT_ARGUMENT,
    TLS13_CLIENT_RANDOM,
    TLS13_CLIENT_IO,
    TLS13_CLIENT_RECORD,
    TLS13_CLIENT_HELLO,
    TLS13_CLIENT_AUTH,
    TLS13_CLIENT_ECHO,
    TLS13_CLIENT_RESPONSE
};

/* Called only after the server is authenticated. A final empty span marks
 * authenticated close_notify. Return false to reject or stop the response. */
typedef bool (*tls13_response_fn)(void *,const unsigned char *,size_t);
enum tls13_client_result tls13_request(const struct tls13_io *,const char *name,
    tls13_trust_fn trust,void *trust_context,const void *request,size_t bytes,
    tls13_response_fn response,void *response_context,enum tls13_auth_result *auth);

/* One bounded TLS 1.3 connection and echo exchange over a connected stream.
 * The entropy callback must supply fresh cryptographic random bytes. */
enum tls13_client_result tls13_echo(const struct tls13_io *,const char *name,
    tls13_trust_fn trust,void *trust_context,const void *message,size_t bytes,enum tls13_auth_result *auth);
#endif
