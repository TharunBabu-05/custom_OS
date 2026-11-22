#include "user.h"
#include "crypto.h"
#include "tls13_client.h"

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

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

static bool peer_address(const char *address,const char *port,struct uos_tcp_peer *peer)
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

struct connection { uint64_t socket;int64_t deadline,error; };

static bool transfer(struct connection *connection,void *in,const void *out,size_t bytes)
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

static bool check_pin(const struct pinned_cert *cert,void *context)
{
    int64_t *policy=context;
    *policy=uos_trust_check(cert->digest,cert->not_before,cert->not_after);
    return *policy==0;
}

static int rng_check(void)
{
    unsigned char first[64]={0},second[64]={0};
    if (uos_random((void *)(uintptr_t)1,32)!=UOS_EFAULT ||
        uos_random(second,0)!=UOS_ERROR || uos_random(second,UOS_RANDOM_MAX+1)!=UOS_ERROR) { return 3; }
    int64_t r=uos_random(first,sizeof(first));
    bool ok=r==0 && uos_random(second,sizeof(second))==0 && !crypto_equal(first,second,sizeof(first));
    bool unchanged=crypto_equal(first,second,sizeof(first));
    crypto_wipe(first,sizeof(first));
    crypto_wipe(second,sizeof(second));
    if (r==UOS_ENORANDOM && unchanged) {
        uos_puts("PASS: random syscall rejected bad arguments and preserved output with RNG unavailable.\n");
        return 0;
    }
    if (!ok) { return 3; }
    uos_puts("PASS: random syscall rejected bad arguments and returned two distinct samples.\n");
    return 0;
}

static int failure(enum tls13_client_result result,enum tls13_auth_result auth,int64_t policy,int64_t network)
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
    else if (result==TLS13_CLIENT_ARGUMENT) { uos_puts("TLS DNS name rejected.\n"); }
    else if (network==UOS_ENETDOWN) { uos_puts("TLS needs an active background Wi-Fi session.\n"); }
    else if (network==UOS_EHOSTUNREACH) { uos_puts("TLS peer did not answer ARP or is off-subnet.\n"); }
    else if (network==UOS_ECONNRESET) { uos_puts("TLS TCP connection was refused or reset.\n"); }
    else if (network==UOS_ETIMEDOUT) { uos_puts("TLS TCP exchange timed out.\n"); }
    else { uos_puts("TLS TCP exchange ended early.\n"); }
    return 1;
}

int app_main(void)
{
    char address[24],port[8],name[254];
    int64_t argc=uos_argc();
    if (argc==2 && uos_arg(1,address,sizeof(address))>0 && same(address,"rng-check")) { return rng_check(); }
    struct uos_tcp_peer peer={0};
    if (argc!=4 || uos_arg(1,address,sizeof(address))<=0 || uos_arg(2,port,sizeof(port))<=0 ||
        uos_arg(3,name,sizeof(name))<=0 || !peer_address(address,port,&peer)) {
        uos_puts("Usage: TLSECHO <LAN IPv4> <TCP port> <DNS name>\n");
        return 2;
    }
    struct uos_utc_time utc;
    if (uos_utc_time(&utc)) { uos_puts("TLS certificate trust requires UTC.\n");return 1; }
    int64_t socket=uos_tcp_open(&peer);
    if (socket<=0) { return failure(TLS13_CLIENT_IO,TLS13_AUTH_FORMAT,0,socket); }
    struct connection connection={(uint64_t)socket,uos_time_ms()+65000,0};
    int64_t connect_deadline=uos_time_ms()+8000;
    do {
        connection.error=uos_tcp_status(connection.socket);
        if (connection.error!=UOS_EAGAIN) { break; }
        uos_sleep_ms(10);
    } while (uos_time_ms()<connect_deadline);
    enum tls13_client_result result=TLS13_CLIENT_IO;
    enum tls13_auth_result auth=TLS13_AUTH_FORMAT;
    int64_t policy=0;
    if (!connection.error) {
        static const unsigned char message[]="ouros-tls";
        const struct tls13_io io={&connection,read_exact,write_all,fresh_random};
        result=tls13_echo(&io,name,check_pin,&policy,message,sizeof(message)-1,&auth);
    } else if (connection.error==UOS_EAGAIN) { connection.error=UOS_ETIMEDOUT; }
    (void)uos_tcp_close(connection.socket);
    if (result!=TLS13_CLIENT_OK) { return failure(result,auth,policy,connection.error); }
    uos_puts("PASS: live TLS 1.3 pinned handshake and encrypted echo.\n");
    return 0;
}
