#include "tls_net.h"
#include "tcp_io.h"

static unsigned decimal(const char **at,unsigned limit)
{
    const char *p=*at;
    if (*p<'0' || *p>'9') { return limit+1; }
    unsigned n=0;
    while (*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p++-'0');
        if (n>(limit-digit)/10) { return limit+1; }
        n=10*n+digit;
    }
    *at=p;
    return n;
}

bool tls_net_peer(const char *address,const char *port,struct uos_tcp_peer *peer)
{
    for (unsigned i=0;i<4;++i) {
        unsigned octet=decimal(&address,255);
        if (octet>255 || *address!=(i==3 ? 0 : '.')) { return false; }
        peer->ipv4[i]=(uint8_t)octet;
        ++address;
    }
    unsigned number=decimal(&port,65535);
    if (!number || number>65535 || *port) { return false; }
    peer->port=(uint16_t)number;
    return true;
}

static bool read_exact(void *context,void *data,size_t bytes)
{
    struct tls_net *net=context;
    net->error=tcp_read_exact(net->socket,data,bytes,net->deadline);
    return !net->error;
}

static bool write_all(void *context,const void *data,size_t bytes)
{
    struct tls_net *net=context;
    net->error=tcp_write_all(net->socket,data,bytes,net->deadline);
    return !net->error;
}

static bool fresh_random(void *context,void *out,size_t bytes)
{ (void)context;return uos_random(out,bytes)==0; }

bool tls_net_check_pin(const struct pinned_cert *cert,void *context)
{
    int64_t *policy=context;
    *policy=uos_trust_check(cert->digest,cert->not_before,cert->not_after);
    return *policy==0;
}


bool tls_net_open(struct tls_net *net,const struct uos_tcp_peer *peer)
{
    int64_t socket=uos_tcp_open(peer);
    net->error=socket;
    if (socket<=0) { return false; }
    net->socket=(uint64_t)socket;
    net->deadline=uos_time_ms()+75000;
    net->error=tcp_wait_connected(net->socket,uos_time_ms()+8000);
    if (net->error) { tls_net_close(net); }
    return !net->error;
}

struct tls13_io tls_net_io(struct tls_net *net)
{ return (struct tls13_io){net,read_exact,write_all,fresh_random}; }

void tls_net_close(struct tls_net *net)
{ if (net->socket) { (void)uos_tcp_close(net->socket);net->socket=0; } }

int tls_net_failure(enum tls13_client_result result,enum tls13_auth_result auth,int64_t policy,int64_t network)
{
    if (result==TLS13_CLIENT_RANDOM) { uos_puts("TLS needs hardware randomness. Run rngtest in the monitor.\n"); }
    else if (result==TLS13_CLIENT_AUTH) {
        if (auth==TLS13_AUTH_NAME) { uos_puts("TLS peer DNS name rejected.\n"); }
        else if (policy==UOS_ENOENT) { uos_puts("TLS peer certificate fingerprint is not enrolled.\n"); }
        else if (policy==UOS_ECERTTIME) { uos_puts("TLS peer certificate is outside its UTC validity window.\n"); }
        else if (policy==UOS_ENOTIME) { uos_puts("TLS certificate trust requires UTC.\n"); }
        else { uos_puts("TLS server authentication rejected.\n"); }
    } else if (result==TLS13_CLIENT_HELLO || result==TLS13_CLIENT_RECORD) {
        uos_puts("TLS record or negotiation rejected.\n");
    } else if (result==TLS13_CLIENT_ECHO) { uos_puts("TLS encrypted echo did not match.\n"); }
    else if (result==TLS13_CLIENT_RESPONSE) { uos_puts("HTTPS response format, size, or completeness rejected.\n"); }
    else if (result==TLS13_CLIENT_ARGUMENT) { uos_puts("TLS DNS name rejected.\n"); }
    else if (network==UOS_ENETDOWN) { uos_puts("TLS needs an active TCP network session.\n"); }
    else if (network==UOS_EHOSTUNREACH) { uos_puts("TLS peer did not answer ARP or is off-subnet.\n"); }
    else if (network==UOS_ECONNRESET) { uos_puts("TLS TCP connection was refused or reset.\n"); }
    else if (network==UOS_ETIMEDOUT) { uos_puts("TLS TCP exchange timed out.\n"); }
    else if (network==UOS_EINTR) { uos_puts("TLS TCP exchange interrupted.\n"); }
    else { uos_puts("TLS TCP exchange ended early.\n"); }
    return 1;
}
