#include "user.h"
#include "sha256.h"

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
static int64_t wait_result(uint64_t handle,void *buffer,size_t bytes,unsigned operation,unsigned ms)
{
    int64_t end=uos_time_ms()+ms;
    for (;;) {
        int64_t r=operation==UOS_SYS_TCP_STATUS ? uos_tcp_status(handle) : operation==UOS_SYS_TCP_READ ?
            uos_tcp_read(handle,buffer,bytes) : uos_tcp_write(handle,buffer,bytes);
        if (r!=UOS_EAGAIN) { return r; }
        if (uos_time_ms()>=end) { return UOS_ETIMEDOUT; }
        uos_sleep_ms(10);
    }
}
static int error(int64_t result)
{
    if (result==UOS_ENETDOWN) { uos_puts("TCP requires an active TCP network session.\n"); }
    else if (result==UOS_EHOSTUNREACH) { uos_puts("TCP peer did not answer ARP or is off-subnet.\n"); }
    else if (result==UOS_ECONNRESET) { uos_puts("TCP peer refused or reset the connection.\n"); }
    else if (result==UOS_ETIMEDOUT) { uos_puts("TCP operation timed out.\n"); }
    else { uos_puts("TCP file echo failed.\n"); }
    return 1;
}
static int64_t echo_file(uint64_t socket,uint64_t file)
{
    struct sha256 hash;sha256_init(&hash);
    unsigned char part[256],reply[256],digest[32];
    for (unsigned total=0;total<16384;) {
        int64_t n=uos_read(file,part,sizeof(part));
        if (n<=0 || (uint64_t)n>16384-total) { return UOS_ERROR; }
        int64_t sent=wait_result(socket,part,(size_t)n,UOS_SYS_TCP_WRITE,5000);
        if (sent!=n) { return sent<0 ? sent : UOS_ERROR; }
        for (size_t done=0;done<(size_t)n;) {
            int64_t got=wait_result(socket,reply,(size_t)n-done,UOS_SYS_TCP_READ,5000);
            if (got<=0) { return got<0 ? got : UOS_ERROR; }
            for (size_t i=0;i<(size_t)got;++i) { if (reply[i]!=part[done+i]) { return UOS_ERROR; } }
            sha256_update(&hash,reply,(size_t)got);done+=(size_t)got;
        }
        total+=(unsigned)n;
    }
    int64_t end=wait_result(socket,reply,1,UOS_SYS_TCP_READ,5000);
    if (end) { return end<0 ? end : UOS_ERROR; }
    sha256_final(&hash,digest);
    const char *digits="0123456789abcdef";char hex[65];
    for (unsigned i=0;i<32;++i) { hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15]; }hex[64]='\n';
    uos_puts("PASS: TCP echoed 16384 bytes. SHA-256 ");uos_write(hex,sizeof(hex));return 0;
}
static int model(void)
{
    static const struct uos_tcp_peer p={{192,0,2,2},34001,0};
    if (uos_tcp_open((const void *)(uintptr_t)1)!=UOS_EFAULT) { return 10; }
    int64_t h=uos_tcp_open(&p);
    if (h<=0) { return 11; }
    uint64_t socket=(uint64_t)h;
    if (wait_result(socket,NULL,0,UOS_SYS_TCP_STATUS,4000)) { return 12; }
    if (uos_tcp_read(socket,(void *)(uintptr_t)&p,1)!=UOS_EFAULT ||
        uos_tcp_write(socket,(void *)(uintptr_t)1,1)!=UOS_EFAULT) { return 13; }
    unsigned char data[]={1,0,2,3,4},reply[5];
    if (uos_tcp_write(socket,data,sizeof(data))!=sizeof(data) || uos_tcp_write(socket,data,1)!=UOS_EAGAIN) { return 14; }
    for (size_t at=0;at<sizeof(reply);) {
        int64_t n=wait_result(socket,reply+at,1,UOS_SYS_TCP_READ,4000);
        if (n!=1 || reply[at]!=data[at]) { return 15; }++at;
    }
    if (wait_result(socket,reply,1,UOS_SYS_TCP_READ,1000) || uos_tcp_close(socket) || uos_tcp_status(socket)!=UOS_EBADF) { return 16; }
    uos_puts("PASS: EL0 TCP connect, checked buffers, ordered stream, EOF and close.\n");return 0;
}
int app_main(void)
{
    char address[24],port[8];int64_t argc=uos_argc();
    if (argc>=2 && uos_arg(1,address,sizeof(address))>0 && argc==2 && same(address,"model")) { return model(); }
    struct uos_tcp_peer peer={0};
    if (argc!=3 || uos_arg(1,address,sizeof(address))<=0 || !parse_ip(address,peer.ipv4) || uos_arg(2,port,sizeof(port))<=0) {
        uos_puts("Usage: TCPCHK <LAN IPv4> <TCP port>\n");return 2;
    }
    const char *p=port;unsigned value=number(&p,65535);
    if (!value || value>65535 || *p) { return error(UOS_ERROR); }peer.port=(uint16_t)value;
    struct uos_entry info;
    if (uos_stat("/boot/LARGE.BIN",&info) || info.bytes!=16384) { uos_puts("TCPCHK requires a 16 KiB /boot/LARGE.BIN.\n");return 2; }
    int64_t file=uos_open("/boot/LARGE.BIN",UOS_READ);if (file<0) { return error(file); }
    int64_t h=uos_tcp_open(&peer),r=h;
    if (h>0) {
        r=wait_result((uint64_t)h,NULL,0,UOS_SYS_TCP_STATUS,8000);
        if (!r) { r=echo_file((uint64_t)h,(uint64_t)file); }
        (void)uos_tcp_close((uint64_t)h);
    }
    (void)uos_close((uint64_t)file);return r ? error(r) : 0;
}
