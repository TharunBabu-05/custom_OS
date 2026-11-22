#ifndef OS_USER_TLS_NET_H
#define OS_USER_TLS_NET_H
#include "user.h"
#include "tls13_client.h"

struct tls_net { uint64_t socket;int64_t deadline,error; };
bool tls_net_peer(const char *address,const char *port,struct uos_tcp_peer *);
bool tls_net_open(struct tls_net *,const struct uos_tcp_peer *);
struct tls13_io tls_net_io(struct tls_net *);
void tls_net_close(struct tls_net *);
bool tls_net_check_pin(const struct pinned_cert *,void *);
int tls_net_failure(enum tls13_client_result,enum tls13_auth_result,int64_t policy,int64_t network);
#endif
