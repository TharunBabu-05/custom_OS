#include "tcp_io.h"
#include "sha256.h"

enum { ECHO_BYTES=16384,ECHO_TIMEOUT_MS=24000,ECHO_STEP_MS=5000,CONNECT_MS=8000 };

static int64_t step_deadline(int64_t limit,unsigned step_ms)
{
    int64_t deadline=uos_time_ms()+step_ms;
    return deadline<limit ? deadline : limit;
}

static int same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static unsigned number(const char **text,unsigned limit)
{
    const char *p=*text;unsigned n=0;
    if (*p<'0' || *p>'9') { return limit+1; }
    while (*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p++-'0');
        if (n>(limit-digit)/10) { return limit+1; }
        n=n*10+digit;
    }
    *text=p;return n;
}
static int parse_ip(const char *p,uint8_t ip[4])
{
    for (unsigned i=0;i<4;++i) {
        unsigned n=number(&p,255);
        if (n>255 || *p!=(i==3 ? 0 : '.')) { return 0; }
        ip[i]=(uint8_t)n;++p;
    }
    return 1;
}
static int error(int64_t result)
{
    if (result==UOS_ENETDOWN) { uos_puts("TCP requires an active TCP network session.\n"); }
    else if (result==UOS_EHOSTUNREACH) { uos_puts("TCP peer did not answer ARP or is off-subnet.\n"); }
    else if (result==UOS_ECONNRESET) { uos_puts("TCP peer refused or reset the connection.\n"); }
    else if (result==UOS_ETIMEDOUT) { uos_puts("TCP operation timed out.\n"); }
    else if (result==UOS_EINTR) { uos_puts("TCP operation interrupted.\n"); }
    else { uos_puts("TCP file echo failed.\n"); }
    return 1;
}
static int64_t finish_send(uint64_t socket)
{
    int64_t result=uos_tcp_shutdown_write(socket);
    if (result) { return result; }
    if (uos_tcp_write(socket,"x",1)!=UOS_EPIPE ||
        uos_socket_wait(UOS_EVENT_TCP,socket,UOS_EVENT_WRITE,0)!=UOS_EVENT_WRITE_CLOSED) {
        return UOS_ERROR;
    }
    return 0;
}

static int64_t receive_after_finish(uint64_t socket,int64_t limit)
{
    static const char expected[]="ouros-fin-ok";
    char trailer[sizeof(expected)-1];
    int64_t left=limit-uos_time_ms();
    if (left<=0) { return UOS_ETIMEDOUT; }
    unsigned wait=left<ECHO_STEP_MS ? (unsigned)left : ECHO_STEP_MS;
    int64_t result=uos_socket_wait(UOS_EVENT_TCP,socket,UOS_EVENT_FIN_ACK,wait);
    if (result<0) { return result; }
    if (!result) { return UOS_ETIMEDOUT; }
    if (result!=UOS_EVENT_FIN_ACK) {
        int64_t status=uos_tcp_status(socket);
        return status ? status : UOS_ERROR;
    }
    struct uos_socket_info info;
    if (uos_socket_info(UOS_EVENT_TCP,socket,&info) || info.version!=UOS_SOCKET_INFO_VERSION ||
        info.state!=UOS_SOCKET_ESTABLISHED || info.tx_bytes || info.error ||
        (info.ready&(UOS_EVENT_FIN_ACK|UOS_EVENT_WRITE_CLOSED))!=(UOS_EVENT_FIN_ACK|UOS_EVENT_WRITE_CLOSED)) {
        return UOS_ERROR;
    }
    result=tcp_read_exact(socket,trailer,sizeof(trailer),step_deadline(limit,ECHO_STEP_MS));
    if (result) { return result; }
    for (size_t i=0;i<sizeof(trailer);++i) { if (trailer[i]!=expected[i]) { return UOS_ERROR; } }
    return 0;
}

static int64_t echo_file(uint64_t socket,uint64_t file,bool half_close,int64_t limit)
{
    struct sha256 hash;sha256_init(&hash);
    unsigned char part[UOS_TCP_MAX_BYTES],reply[UOS_TCP_MAX_BYTES],digest[32];
    for (unsigned total=0;total<ECHO_BYTES;) {
        int64_t n=uos_read(file,part,sizeof(part));
        if (n<=0 || (uint64_t)n>ECHO_BYTES-total) { return UOS_ERROR; }
        int64_t result=tcp_write_all(socket,part,(size_t)n,step_deadline(limit,ECHO_STEP_MS));
        if (result) { return result; }
        if (half_close && total+(size_t)n==ECHO_BYTES) {
            result=finish_send(socket);
            if (result) { return result; }
        }
        result=tcp_read_exact(socket,reply,(size_t)n,step_deadline(limit,ECHO_STEP_MS));
        if (result) { return result; }
        for (size_t i=0;i<(size_t)n;++i) { if (reply[i]!=part[i]) { return UOS_ERROR; } }
        sha256_update(&hash,reply,(size_t)n);
        total+=(unsigned)n;
    }
    if (half_close) {
        int64_t result=receive_after_finish(socket,limit);
        if (result) { return result; }
    }
    int64_t end=tcp_read_until(socket,reply,1,step_deadline(limit,ECHO_STEP_MS));
    if (end) { return end<0 ? end : UOS_ERROR; }
    sha256_final(&hash,digest);
    const char *digits="0123456789abcdef";char hex[65];
    for (unsigned i=0;i<32;++i) { hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15]; }hex[64]='\n';
    uos_puts("PASS: TCP echoed 16384 bytes. SHA-256 ");uos_write(hex,sizeof(hex));
    if (half_close) {
        uos_puts("PASS: TCP write half-close acknowledged and final reply received before EOF.\n");
        uos_puts("PASS: owned TCP socket snapshot confirmed FIN acknowledgment and drained send queue.\n");
    }
    return 0;
}
static int model(void)
{
    static const struct uos_tcp_peer p={{192,0,2,2},34001,0};
    if (uos_tcp_open((const void *)(uintptr_t)1)!=UOS_EFAULT) { return 10; }
    int64_t h=uos_tcp_open(&p);
    if (h<=0) { return 11; }
    uint64_t socket=(uint64_t)h;
    if (tcp_wait_connected(socket,uos_time_ms())!=UOS_ETIMEDOUT ||
        tcp_wait_connected(socket,uos_time_ms()+4000)) { return 12; }
    if (uos_socket_wait(UOS_EVENT_TCP,socket,UOS_EVENT_READ|UOS_EVENT_WRITE,0)!=UOS_EVENT_WRITE) { return 17; }
    if (uos_tcp_read(socket,(void *)(uintptr_t)&p,1)!=UOS_EFAULT ||
        uos_tcp_write(socket,(void *)(uintptr_t)1,1)!=UOS_EFAULT) { return 13; }
    unsigned char data[]={1,0,2,3,4},reply[5];
    int64_t deadline=uos_time_ms()+40;
    if (tcp_read_until(socket,reply,1,deadline)!=UOS_ETIMEDOUT || uos_time_ms()<deadline) { return 18; }
    if (uos_tcp_write(socket,data,sizeof(data))!=sizeof(data) || uos_tcp_write(socket,data,1)!=UOS_EAGAIN) { return 14; }
    for (size_t at=0;at<sizeof(reply);) {
        int64_t n=tcp_read_until(socket,reply+at,1,uos_time_ms()+4000);
        if (n!=1 || reply[at]!=data[at]) { return 15; }++at;
    }
    if (uos_socket_wait(UOS_EVENT_TCP,socket,UOS_EVENT_READ,1000)!=UOS_EVENT_READ ||
        tcp_read_exact(socket,reply,1,uos_time_ms()+1000)!=UOS_EOF ||
        uos_tcp_close(socket) || uos_tcp_status(socket)!=UOS_EBADF ||
        uos_socket_wait(UOS_EVENT_TCP,socket,UOS_EVENT_READ,0)!=UOS_EVENT_HANGUP) { return 16; }
    uos_puts("PASS: EL0 TCP connect, checked buffers, ordered stream, EOF and close.\n");
    uos_puts("PASS: TCP runtime rejects expired deadlines and reports short EOF.\n");
    return 0;
}
int app_main(void)
{
    char address[24],port[8],mode[8];int64_t argc=uos_argc();
    if (argc>=2 && uos_arg(1,address,sizeof(address))>0 && argc==2 && same(address,"model")) { return model(); }
    struct uos_tcp_peer peer={0};
    bool half_close=argc==4 && uos_arg(3,mode,sizeof(mode))>0 && same(mode,"half");
    if ((argc!=3 && !half_close) || uos_arg(1,address,sizeof(address))<=0 || !parse_ip(address,peer.ipv4) ||
        uos_arg(2,port,sizeof(port))<=0) {
        uos_puts("Usage: TCPCHK <LAN IPv4> <TCP port> [half]\n");return 2;
    }
    const char *p=port;unsigned value=number(&p,65535);
    if (!value || value>65535 || *p) { return error(UOS_ERROR); }peer.port=(uint16_t)value;
    struct uos_entry info;
    if (uos_stat("/boot/LARGE.BIN",&info) || info.bytes!=ECHO_BYTES) { uos_puts("TCPCHK requires a 16 KiB /boot/LARGE.BIN.\n");return 2; }
    int64_t file=uos_open("/boot/LARGE.BIN",UOS_READ);if (file<0) { return error(file); }
    int64_t h=uos_tcp_open(&peer),r=h;
    if (h>0) {
        int64_t limit=uos_time_ms()+ECHO_TIMEOUT_MS;
        r=tcp_wait_connected((uint64_t)h,step_deadline(limit,CONNECT_MS));
        if (!r) { r=echo_file((uint64_t)h,(uint64_t)file,half_close,limit); }
        (void)uos_tcp_close((uint64_t)h);
    }
    (void)uos_close((uint64_t)file);return r ? error(r) : 0;
}
