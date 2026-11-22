#include "tls_net.h"

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

static bool transfer(struct tls_net *connection,void *in,const void *out,size_t bytes)
{
    while (bytes && uos_time_ms()<connection->deadline) {
        size_t n=bytes<UOS_TCP_MAX_BYTES ? bytes : UOS_TCP_MAX_BYTES;
        int64_t result=in ? uos_tcp_read(connection->socket,in,n) : uos_tcp_write(connection->socket,out,n);
        if (result==UOS_EAGAIN) { uos_sleep_ms(10);continue; }
        if (result<=0 || (uint64_t)result>n) { connection->error=result;return false; }
        if (in) { in=(unsigned char *)in+result; }
        else { out=(const unsigned char *)out+result; }
        bytes-=(size_t)result;
    }
    if (bytes) { connection->error=UOS_ETIMEDOUT; }
    return !bytes;
}

static bool read_exact(void *context,void *data,size_t bytes)
{ return transfer(context,data,NULL,bytes); }

static bool write_all(void *context,const void *data,size_t bytes)
{ return transfer(context,NULL,data,bytes); }

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
    int64_t connect_deadline=uos_time_ms()+8000;
    do {
        net->error=uos_tcp_status(net->socket);
        if (net->error!=UOS_EAGAIN) { return net->error==0; }
        uos_sleep_ms(10);
    } while (uos_time_ms()<connect_deadline);
    net->error=UOS_ETIMEDOUT;
    return false;
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
    else if (network==UOS_ENETDOWN) { uos_puts("TLS needs an active background Wi-Fi session.\n"); }
    else if (network==UOS_EHOSTUNREACH) { uos_puts("TLS peer did not answer ARP or is off-subnet.\n"); }
    else if (network==UOS_ECONNRESET) { uos_puts("TLS TCP connection was refused or reset.\n"); }
    else if (network==UOS_ETIMEDOUT) { uos_puts("TLS TCP exchange timed out.\n"); }
    else { uos_puts("TLS TCP exchange ended early.\n"); }
    return 1;
}
