#include "user.h"

static int same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static uint64_t decimal(const char **input,uint64_t limit)
{
    const char *p=*input;uint64_t value=0;
    if (*p<'0' || *p>'9') { return limit+1; }
    while (*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p++-'0');
        if (value>(limit-digit)/10) { return limit+1; }
        value=value*10+digit;
    }
    *input=p;return value;
}
static int parse_ip(const char *p,uint8_t ip[4])
{
    for (unsigned i=0;i<4;++i) {
        uint64_t value=decimal(&p,255);
        if (value>255 || *p!=(i==3 ? 0 : '.')) { return 0; }
        ip[i]=(uint8_t)value;++p;
    }
    return 1;
}
static int report(int64_t error)
{
    if (error==UOS_ENETDOWN) { uos_puts("UDP requires an active background Wi-Fi session.\n"); }
    else if (error==UOS_EHOSTUNREACH) { uos_puts("UDP LAN peer did not answer ARP or is off-subnet.\n"); }
    else if (error==UOS_ETIMEDOUT) { uos_puts("UDP echo timed out.\n"); }
    else { uos_puts("UDP echo failed.\n"); }
    return 1;
}
static int echo(const struct uos_udp_peer *peer,const char *message)
{
    int64_t h=uos_udp_open(peer);
    if (h<0) { return report(h); }
    size_t bytes=uos_length(message);
    int64_t result=uos_udp_send((uint64_t)h,message,bytes);
    unsigned char reply[UOS_UDP_MAX_BYTES];
    if (result>=0) { result=uos_udp_recv((uint64_t)h,reply,sizeof(reply),5000); }
    if (uos_udp_close((uint64_t)h) && result>=0) { return report(UOS_EBADF); }
    if (result!=(int64_t)bytes) { return report(result); }
    for (size_t i=0;i<bytes;++i) {
        if (reply[i]!=(unsigned char)message[i]) { return report(UOS_ERROR); }
    }
    uos_puts("PASS: UDP echo matched the sent datagram.\n");return 0;
}

/* QEMU's worker supplies this test-only peer. The same calls pass through the
 * production syscall and transport-independent socket code. */
static const struct uos_udp_peer model_peer={{192,0,2,2},32001,55001};
static void argument(struct app_args *args,const char *s)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*s; } while (*s++);
}
static void number(char text[24],uint64_t n)
{
    char reverse[24];unsigned count=0;
    do { reverse[count++]=(char)('0'+n%10);n/=10; } while (n);
    for (unsigned i=0;i<count;++i) { text[i]=reverse[count-1-i]; }
    text[count]=0;
}
static int foreign(void)
{
    char text[24];
    if (uos_arg(2,text,sizeof(text))<=0) { return 2; }
    const char *p=text;uint64_t stolen=decimal(&p,INT64_MAX);
    if (*p || !stolen || stolen>INT64_MAX || uos_udp_send(stolen,NULL,0)!=UOS_EBADF ||
        uos_udp_recv(stolen,NULL,0,0)!=UOS_EBADF || uos_udp_close(stolen)!=UOS_EBADF ||
        uos_udp_open(&model_peer)!=UOS_EBUSY) { return 3; }
    struct uos_udp_peer own=model_peer;own.local_port=55002;
    return uos_udp_open(&own)>0 ? 0 : 4; /* Kernel closes this on exit. */
}
static int model(void)
{
    if (uos_udp_open((const void *)(uintptr_t)1)!=UOS_EFAULT) { return 10; }
    int64_t h=uos_udp_open(&model_peer);
    if (h<=0) { return 11; }
    uint64_t handle=(uint64_t)h;
    unsigned char buffer[8];
    if (uos_udp_recv(handle,buffer,sizeof(buffer),0)!=UOS_EAGAIN || uos_udp_send(handle,(const void *)(uintptr_t)1,1)!=UOS_EFAULT ||
        uos_udp_recv(handle,(void *)(uintptr_t)&model_peer,1,0)!=UOS_EFAULT ||
        uos_udp_send(handle,buffer,UOS_UDP_MAX_BYTES+1)!=UOS_EMSGSIZE ||
        uos_udp_recv(handle,buffer,sizeof(buffer),UOS_UDP_MAX_WAIT_MS+1)!=UOS_ERROR) { return 12; }

    struct app_args args={0};char text[24];number(text,handle);
    argument(&args,"/boot/UDPCHK.ELF");argument(&args,"foreign");argument(&args,text);
    int64_t child=uos_spawn(&args,2);
    if (child<=0 || uos_wait((uint64_t)child)!=0) { return 13; }
    struct uos_udp_peer owned=model_peer;owned.local_port=55002;
    int64_t second=uos_udp_open(&owned);
    owned.local_port=55003;
    if (second<=0 || uos_udp_open(&owned)!=UOS_ENOSPC || uos_udp_close((uint64_t)second) ||
        uos_udp_recv((uint64_t)second,NULL,0,0)!=UOS_EBADF) { return 14; }

    const unsigned char payload[]={0xc5,0,0x21};
    if (uos_udp_send(handle,payload,sizeof(payload))!=(int64_t)sizeof(payload) ||
        uos_udp_recv(handle,buffer,1,1000)!=UOS_EMSGSIZE ||
        uos_udp_recv(handle,buffer,sizeof(buffer),0)!=(int64_t)sizeof(payload)) { return 15; }
    for (unsigned i=0;i<sizeof(payload);++i) { if (buffer[i]!=payload[i]) { return 16; } }
    if (uos_udp_send(handle,NULL,0)!=0 || uos_udp_recv(handle,NULL,0,1000)!=0 ||
        uos_udp_recv(handle,buffer,sizeof(buffer),100)!=UOS_ETIMEDOUT) { return 17; }
    uos_puts("PASS: EL0 UDP ownership, datagrams, checked buffers and receive timeout.\n");
    return 0; /* The parent handle also exercises process cleanup. */
}
static int loss(void)
{
    struct uos_udp_peer peer=model_peer;peer.port=32002;
    int64_t h=uos_udp_open(&peer);
    if (h<=0 || uos_udp_send((uint64_t)h,NULL,0)!=0 || uos_udp_recv((uint64_t)h,NULL,0,1000)!=UOS_ENETDOWN ||
        uos_udp_close((uint64_t)h)!=UOS_EBADF || uos_udp_open(&peer)!=UOS_ENETDOWN) { return 20; }
    uos_puts("PASS: UDP link loss woke the blocked receive and invalidated its handle.\n");return 0;
}

int app_main(void)
{
    int64_t argc=uos_argc();char name[24],port[8];
    if (argc>=2 && uos_arg(1,name,sizeof(name))>0) {
        if (argc==2 && same(name,"model")) { return model(); }
        if (argc==2 && same(name,"loss")) { return loss(); }
        if (argc==3 && same(name,"foreign")) { return foreign(); }
        struct uos_udp_peer peer={0};
        if ((argc==3 || argc==4) && parse_ip(name,peer.ipv4) && uos_arg(2,port,sizeof(port))>0) {
            const char *p=port;uint64_t value=decimal(&p,65535);
            if (value && value<=65535 && !*p) {
                char message[UOS_UDP_MAX_BYTES+1]="OurOS UDP echo";
                if (argc==3 || uos_arg(3,message,sizeof(message))>=0) { peer.port=(uint16_t)value;return echo(&peer,message); }
            }
        }
    }
    uos_puts("Usage: UDPCHK <LAN IPv4> <UDP port> [message]\n");return 2;
}
