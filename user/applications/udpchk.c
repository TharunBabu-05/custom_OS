#include "udp_io.h"
#include "net_wire.h"

enum { PROBE_ROUNDS_MAX=4, PROBE_ATTEMPTS=3, PROBE_SEND_MS=4000, PROBE_REPLY_MS=1500, PROBE_TOTAL_MS=24000 };

static int same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static uint64_t decimal(const char **input,uint64_t limit)
{
    const char *p=*input;uint64_t value=0;
    if (*p<'0' || *p>'9') { return limit+1; }
    while (*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p++-'0');
        if (digit>limit || value>(limit-digit)/10) { return limit+1; }
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
    if (error==UOS_ENETDOWN) { uos_puts("UDP requires an active network session.\n"); }
    else if (error==UOS_EHOSTUNREACH) { uos_puts("UDP LAN peer did not answer ARP or is off-subnet.\n"); }
    else if (error==UOS_ETIMEDOUT) { uos_puts("UDP echo timed out. Check the listener address and port.\n"); }
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
    if (result>=0) {
        result=uos_socket_wait(UOS_EVENT_UDP,(uint64_t)h,UOS_EVENT_READ,5000);
        if (!result) { result=UOS_ETIMEDOUT; }
        else if (result>0) {
            if (result&UOS_EVENT_READ) {
                struct uos_socket_info info;
                result=uos_socket_info(UOS_EVENT_UDP,(uint64_t)h,&info);
                if (!result && (info.version!=UOS_SOCKET_INFO_VERSION || info.state!=UOS_SOCKET_DATAGRAM ||
                    !(info.ready&UOS_EVENT_READ) || !(info.flags&UOS_SOCKET_RX_PENDING) || info.rx_bytes!=bytes)) {
                    result=UOS_ERROR;
                }
            }
            if (result>=0) { result=uos_udp_recv((uint64_t)h,reply,sizeof(reply),0); }
        }
    }
    if (uos_udp_close((uint64_t)h) && result>=0) { return report(UOS_EBADF); }
    if (result!=(int64_t)bytes) { return report(result); }
    for (size_t i=0;i<bytes;++i) {
        if (reply[i]!=(unsigned char)message[i]) { return report(UOS_ERROR); }
    }
    uos_puts("PASS: UDP echo matched the sent datagram.\n");
    uos_puts("PASS: owned UDP socket snapshot confirmed receive queue length.\n");
    return 0;
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
static void metric(const char *label,uint64_t value)
{
    char text[24];number(text,value);uos_puts(label);uos_puts(text);
}
static int64_t probe_deadline(int64_t limit,unsigned window)
{
    int64_t next=uos_time_ms()+window;
    return next<limit ? next : limit;
}
static int64_t probe_round(uint64_t socket,uint64_t pid,unsigned round,int64_t limit,unsigned *sent,unsigned *ignored)
{
    unsigned char request[16]={'O','U','P','1'},reply[UOS_UDP_MAX_BYTES];
    net_put32(request+4,(uint32_t)(pid>>32));
    net_put32(request+8,(uint32_t)pid);
    net_put32(request+12,round);
    int64_t started=uos_time_ms();
    for (unsigned attempt=1;attempt<=PROBE_ATTEMPTS;++attempt) {
        int64_t deadline=probe_deadline(limit,PROBE_SEND_MS);
        int64_t result=udp_send_until(socket,request,sizeof(request),deadline);
        if (result>=0) { result=result==(int64_t)sizeof(request) ? udp_flush_until(socket,deadline) : UOS_ERROR; }
        if (result) { uos_puts("UDP probe send did not complete.\n");return result; }
        ++*sent;
        deadline=probe_deadline(limit,PROBE_REPLY_MS);
        for (;;) {
            result=udp_recv_until(socket,reply,sizeof(reply),deadline);
            if (result==UOS_ETIMEDOUT) { break; }
            if (result<0) { return result; }
            if (result==(int64_t)sizeof(request) && net_equal(request,reply,sizeof(request))) {
                metric("probe=",round);
                metric(" attempts=",attempt);
                metric(" elapsed_ms=",(uint64_t)(uos_time_ms()-started));
                uos_puts("\n");
                return 0;
            }
            ++*ignored;
        }
        if (uos_time_ms()>=limit) { break; }
    }
    uos_puts("UDP probe sent but no matching reply arrived.\n");
    return UOS_ETIMEDOUT;
}
static int probe(const struct uos_udp_peer *peer,unsigned rounds)
{
    int64_t handle=uos_udp_open(peer);
    if (handle<0) { return report(handle); }
    const int64_t limit=uos_time_ms()+PROBE_TOTAL_MS;
    const uint64_t pid=(uint64_t)uos_getpid();
    unsigned sent=0,ignored=0,matched=0;
    int64_t result=0;
    while (matched<rounds) {
        result=probe_round((uint64_t)handle,pid,matched+1,limit,&sent,&ignored);
        if (result) { break; }
        ++matched;
    }
    int64_t closed=uos_udp_close((uint64_t)handle);
    metric("UDP probes matched=",matched);metric("/",rounds);
    metric(" sent=",sent);metric(" ignored=",ignored);uos_puts("\n");
    if (result || closed) { return report(result ? result : closed); }
    uos_puts("PASS: all UDP probe replies matched.\n");
    return 0;
}
static int foreign(void)
{
    char text[24];
    if (uos_arg(2,text,sizeof(text))<=0) { return 2; }
    const char *p=text;uint64_t stolen=decimal(&p,INT64_MAX);
    struct uos_socket_info info={.version=UINT32_MAX};
    if (*p || !stolen || stolen>INT64_MAX || uos_udp_send(stolen,NULL,0)!=UOS_EBADF ||
        uos_udp_recv(stolen,NULL,0,0)!=UOS_EBADF || uos_udp_close(stolen)!=UOS_EBADF ||
        uos_socket_wait(UOS_EVENT_UDP,stolen,UOS_EVENT_READ,0)!=UOS_EVENT_HANGUP ||
        uos_socket_info(UOS_EVENT_UDP,stolen,&info)!=UOS_EBADF || info.version!=UINT32_MAX ||
        uos_udp_open(&model_peer)!=UOS_EBUSY) { return 3; }
    struct uos_udp_peer own=model_peer;own.local_port=55002;
    int64_t handle=uos_udp_open(&own);
    if (handle<0 || uos_policy_restrict(UOS_POLICY_ALL&~UOS_ALLOW_NETWORK)) { return 4; }
    struct uos_event event={.object=(uint64_t)handle,.source=UOS_EVENT_UDP,.events=UOS_EVENT_READ,.revents=123};
    return uos_event_wait(&event,1,0)==UOS_EACCES && event.revents==123 ? 0 : 5;
}
static int model(void)
{
    if (uos_udp_open((const void *)(uintptr_t)1)!=UOS_EFAULT) { return 10; }
    int64_t h=uos_udp_open(&model_peer);
    if (h<=0) { return 11; }
    uint64_t handle=(uint64_t)h;
    unsigned char buffer[8];
    if (uos_socket_wait(UOS_EVENT_UDP,handle,UOS_EVENT_READ|UOS_EVENT_WRITE,0)!=UOS_EVENT_WRITE ||
        uos_socket_wait(UOS_EVENT_UDP,handle,UOS_EVENT_ERROR,0)!=UOS_ERROR) { return 18; }
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
    int64_t timer=uos_timer_open();
    if (timer<=0 || uos_timer_arm((uint64_t)timer,1000,0)) { return 19; }
    struct uos_event events[]={
        {.object=handle,.source=UOS_EVENT_UDP,.events=UOS_EVENT_READ},
        {.object=(uint64_t)timer,.source=UOS_EVENT_TIMER,.events=UOS_EVENT_READ}
    };
    if (uos_udp_send(handle,payload,sizeof(payload))!=(int64_t)sizeof(payload) ||
        uos_event_wait(events,2,1200)!=1 || events[0].revents!=UOS_EVENT_READ || events[1].revents ||
        uos_udp_recv(handle,buffer,1,0)!=UOS_EMSGSIZE ||
        uos_socket_wait(UOS_EVENT_UDP,handle,UOS_EVENT_READ,0)!=UOS_EVENT_READ ||
        uos_udp_recv(handle,buffer,sizeof(buffer),0)!=(int64_t)sizeof(payload)) { return 15; }
    for (unsigned i=0;i<sizeof(payload);++i) { if (buffer[i]!=payload[i]) { return 16; } }
    struct uos_socket_info info;
    if (uos_udp_send(handle,NULL,0)!=0 || uos_socket_wait(UOS_EVENT_UDP,handle,UOS_EVENT_READ,1000)!=UOS_EVENT_READ ||
        uos_socket_info(UOS_EVENT_UDP,handle,&info) || info.version!=UOS_SOCKET_INFO_VERSION ||
        info.state!=UOS_SOCKET_DATAGRAM || !(info.flags&UOS_SOCKET_RX_PENDING) || info.rx_bytes ||
        uos_udp_recv(handle,NULL,0,0)!=0 || uos_timer_arm((uint64_t)timer,70,0) ||
        uos_event_wait(events,2,1000)!=1 || events[0].revents || events[1].revents!=UOS_EVENT_READ ||
        uos_timer_close((uint64_t)timer) ||
        uos_udp_recv(handle,buffer,sizeof(buffer),100)!=UOS_ETIMEDOUT) { return 17; }
    int64_t now=uos_time_ms();
    if (udp_send_until(handle,NULL,0,now)!=UOS_ETIMEDOUT ||
        udp_send_until(handle,NULL,0,now+1000)!=0 || udp_flush_until(handle,now+1000) ||
        udp_recv_until(handle,NULL,0,now+1000)!=0 || udp_recv_until(handle,buffer,sizeof(buffer),now)!=UOS_ETIMEDOUT) {
        return 21;
    }
    uos_puts("PASS: EL0 UDP ownership, datagrams, checked buffers and receive timeout.\n");
    uos_puts("PASS: UDP readiness, timer race and restricted wait.\n");
    uos_puts("PASS: UDP socket snapshot preserves empty datagram and ownership semantics.\n");
    uos_puts("PASS: UDP runtime preserves empty datagrams and absolute deadlines.\n");
    return 0; /* The parent handle also exercises process cleanup. */
}
static int loss(bool readiness)
{
    struct uos_udp_peer peer=model_peer;peer.port=32002;
    int64_t h=uos_udp_open(&peer);
    if (h<=0 || uos_udp_send((uint64_t)h,NULL,0)!=0 ||
        (readiness && uos_socket_wait(UOS_EVENT_UDP,(uint64_t)h,UOS_EVENT_READ,1000)!=UOS_EVENT_HANGUP) ||
        uos_udp_recv((uint64_t)h,NULL,0,readiness ? 0 : 1000)!=UOS_ENETDOWN ||
        uos_udp_close((uint64_t)h)!=UOS_EBADF || uos_udp_open(&peer)!=UOS_ENETDOWN) { return 20; }
    uos_puts(readiness ? "PASS: UDP link loss woke the socket wait.\n" :
             "PASS: UDP link loss woke the blocked receive and invalidated its handle.\n");
    return 0;
}

int app_main(void)
{
    int64_t argc=uos_argc();char name[24],port[8];
    if (argc>=2 && uos_arg(1,name,sizeof(name))>0) {
        if (argc==2 && same(name,"model")) { return model(); }
        if (argc==2 && same(name,"loss")) { return loss(false); }
        if (argc==2 && same(name,"waitloss")) { return loss(true); }
        if (argc==3 && same(name,"foreign")) { return foreign(); }
        if (argc==5 && same(name,"probe")) {
            char address[24],count[8];
            struct uos_udp_peer peer={0};
            if (uos_arg(2,address,sizeof(address))>0 && parse_ip(address,peer.ipv4) &&
                uos_arg(3,port,sizeof(port))>0 && uos_arg(4,count,sizeof(count))>0) {
                const char *port_text=port,*count_text=count;
                uint64_t target=decimal(&port_text,65535),rounds=decimal(&count_text,PROBE_ROUNDS_MAX);
                if (target && target<=65535 && !*port_text && rounds && rounds<=PROBE_ROUNDS_MAX && !*count_text) {
                    peer.port=(uint16_t)target;return probe(&peer,(unsigned)rounds);
                }
            }
        }
        struct uos_udp_peer peer={0};
        if ((argc==3 || argc==4) && parse_ip(name,peer.ipv4) && uos_arg(2,port,sizeof(port))>0) {
            const char *p=port;uint64_t value=decimal(&p,65535);
            if (value && value<=65535 && !*p) {
                char message[UOS_UDP_MAX_BYTES+1]="OurOS UDP echo";
                if (argc==3 || uos_arg(3,message,sizeof(message))>=0) { peer.port=(uint16_t)value;return echo(&peer,message); }
            }
        }
    }
    uos_puts("Usage: UDPCHK <LAN IPv4> <UDP port> [message]\n       UDPCHK probe <LAN IPv4> <UDP port> <1..4>\n");return 2;
}
