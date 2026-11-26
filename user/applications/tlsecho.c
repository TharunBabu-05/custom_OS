#include "user.h"
#include "crypto.h"
#include "tls_net.h"

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
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

/* The QEMU test peer echoes 700 bytes, then closes its send side. */
static int transport_check(void)
{
    static const struct uos_tcp_peer peer={{192,0,2,2},34003,0};
    unsigned char sent[700],received[700],extra;
    for (unsigned i=0;i<sizeof(sent);++i) { sent[i]=(unsigned char)(i*29); }
    struct tls_net net={0};
    if (!tls_net_open(&net,&peer)) { return 3; }
    struct tls13_io io=tls_net_io(&net);
    bool ok=io.write_all(io.context,sent,sizeof(sent)) && io.read_exact(io.context,received,sizeof(received)) &&
        crypto_equal(sent,received,sizeof(sent)) && !io.read_exact(io.context,&extra,1) && net.error==UOS_EOF;
    tls_net_close(&net);
    if (!ok) { return 4; }
    uos_puts("PASS: TLS transport callbacks exchanged 700 bytes and reported EOF.\n");
    return 0;
}

int app_main(void)
{
    char address[24],port[8],name[254];
    int64_t argc=uos_argc();
    if (argc==2 && uos_arg(1,address,sizeof(address))>0) {
        if (same(address,"rng-check")) { return rng_check(); }
        if (same(address,"transport-check")) { return transport_check(); }
    }
    struct uos_tcp_peer peer={0};
    if (argc!=4 || uos_arg(1,address,sizeof(address))<=0 || uos_arg(2,port,sizeof(port))<=0 ||
        uos_arg(3,name,sizeof(name))<=0 || !tls_net_peer(address,port,&peer)) {
        uos_puts("Usage: TLSECHO <LAN IPv4> <TCP port> <DNS name>\n");
        return 2;
    }
    struct uos_utc_time utc;
    if (uos_utc_time(&utc)) { uos_puts("TLS certificate trust requires UTC.\n");return 1; }
    struct tls_net net={0};
    if (!tls_net_open(&net,&peer)) {
        tls_net_close(&net);
        return tls_net_failure(TLS13_CLIENT_IO,TLS13_AUTH_FORMAT,0,net.error);
    }
    static const unsigned char message[]="ouros-tls";
    const struct tls13_io io=tls_net_io(&net);
    enum tls13_auth_result auth=TLS13_AUTH_FORMAT;
    int64_t policy=0;
    enum tls13_client_result result=tls13_echo(&io,name,tls_net_check_pin,&policy,message,sizeof(message)-1,&auth);
    tls_net_close(&net);
    if (result!=TLS13_CLIENT_OK) { return tls_net_failure(result,auth,policy,net.error); }
    uos_puts("PASS: live TLS 1.3 pinned handshake and encrypted echo.\n");
    return 0;
}
