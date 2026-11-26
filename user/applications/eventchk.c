#include "user.h"

static const char app[]="/boot/EVENTCHK.ELF";
static const char port_name[]="event38";
enum { WAIT_MS=2000,ECHO_MS=1200 };

static char *decimal(char text[21],uint64_t value)
{
    unsigned at=20;
    text[at]=0;
    do { text[--at]=(char)('0'+value%10);value/=10; } while (value);
    return text+at;
}
static int failure(unsigned line)
{
    char text[21];
    uos_puts("FAIL: EVENTCHK line ");uos_puts(decimal(text,line));uos_puts("\n");
    return 1;
}
#define CHECK(test) do { if (!(test)) { return failure(__LINE__); } } while (0)

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}
static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}
static int64_t start(const char *mode,const char *value)
{
    struct app_args args={0};
    char pid[21];
    word(&args,app);word(&args,mode);
    word(&args,value ? value : decimal(pid,(uint64_t)uos_getpid()));
    return uos_spawn(&args,10);
}
static struct uos_event watch(unsigned source,uint64_t object,unsigned events)
{
    return (struct uos_event){.object=object,.source=source,.events=(uint16_t)events};
}
static int64_t one(unsigned source,uint64_t object,unsigned events,unsigned wait)
{
    struct uos_event row=watch(source,object,events);
    int64_t ready=uos_event_wait(&row,1,wait);
    return ready<0 ? ready : row.revents;
}
static bool send_direct(uint64_t peer,uint64_t value)
{
    struct uos_message message={.peer=peer,.bytes=8};
    for (unsigned i=0;i<8;++i) { message.data[i]=(unsigned char)(value>>(i*8)); }
    return !uos_ipc_send(&message,WAIT_MS);
}
static uint64_t recv_direct(uint64_t peer)
{
    struct uos_message message;
    if (uos_ipc_recv(peer,&message,WAIT_MS) || message.bytes!=8 || message.handle) { return 0; }
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) { value|=(uint64_t)message.data[i]<<(i*8); }
    return value;
}
static bool send_byte(uint64_t channel,unsigned char byte,unsigned wait)
{
    struct uos_message message={.bytes=1,.data={byte}};
    return !uos_channel_send(channel,&message,wait);
}
static bool recv_byte(uint64_t channel,unsigned char byte,unsigned wait)
{
    struct uos_message message;
    return !uos_channel_recv(channel,&message,wait) && message.peer && !message.handle &&
        message.bytes==1 && message.data[0]==byte;
}
static bool blocked(uint64_t pid)
{
    for (unsigned i=0;i<100;++i) {
        struct uos_child_info child;
        if (uos_child_poll(pid,&child) || child.finished) { return false; }
        if (child.state==UOS_PROCESS_IPC_RECV) { return true; }
        if (uos_sleep_ms(10)) { return false; }
    }
    return false;
}
static int argument_checks(void)
{
    struct uos_event row=watch(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ);
    CHECK(uos_event_wait(0,1,0)==UOS_EFAULT && uos_event_wait(&row,0,0)==UOS_ERROR &&
          uos_event_wait(&row,UOS_EVENT_MAX+1,0)==UOS_ERROR && uos_event_wait(&row,1,UOS_WAIT_MAX_MS+1)==UOS_ERROR);
    row.events=UOS_EVENT_WRITE;
    CHECK(uos_event_wait(&row,1,0)==UOS_ERROR);
    row.events=UOS_EVENT_READ;row.source=0;
    CHECK(uos_event_wait(&row,1,0)==UOS_ERROR);
    row.source=UOS_EVENT_SIGNAL;row.revents=UINT16_MAX;
    CHECK(!uos_event_wait(&row,1,0) && !row.revents);
    int64_t start_ms=uos_time_ms();
    CHECK(!uos_event_wait(&row,1,40) && !row.revents && uos_time_ms()-start_ms>=40);
    CHECK(one(UOS_EVENT_CHANNEL,UINT64_MAX,UOS_EVENT_READ,0)==UOS_EVENT_HANGUP);
    return 0;
}
static int channel_peer(uint64_t parent)
{
    int64_t link=uos_port_connect(port_name,parent);
    CHECK(link>0 && !one(UOS_EVENT_CHANNEL,(uint64_t)link,UOS_EVENT_READ|UOS_EVENT_WRITE,0));
    CHECK(send_direct(parent,(uint64_t)link) && uos_signal_wait(WAIT_MS)==UOS_SIGUSR1);
    CHECK(one(UOS_EVENT_CHANNEL,(uint64_t)link,UOS_EVENT_READ,0)==UOS_EVENT_READ);
    for (unsigned i=0;i<UOS_IPC_DEPTH;++i) { CHECK(recv_byte((uint64_t)link,(unsigned char)i,WAIT_MS)); }
    CHECK(send_byte((uint64_t)link,'r',WAIT_MS) && recv_byte((uint64_t)link,'x',WAIT_MS));
    return 0;
}
static int channel_checks(void)
{
    int64_t port=uos_port_listen(port_name);
    CHECK(port>0 && !one(UOS_EVENT_LISTENER,(uint64_t)port,UOS_EVENT_READ,0));
    int64_t pid=start("peer",0);
    CHECK(pid>0 && one(UOS_EVENT_MAILBOX,(uint64_t)pid,UOS_EVENT_READ,WAIT_MS)==UOS_EVENT_READ);
    struct uos_event events[]={
        watch(UOS_EVENT_LISTENER,(uint64_t)port,UOS_EVENT_READ),
        watch(UOS_EVENT_MAILBOX,(uint64_t)pid,UOS_EVENT_READ),
        watch(UOS_EVENT_CHILD,(uint64_t)pid,UOS_EVENT_READ),
        watch(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ)
    };
    CHECK(uos_event_wait(events,4,0)==2 && events[0].revents==UOS_EVENT_READ && events[1].revents==UOS_EVENT_READ &&
          !events[2].revents && !events[3].revents);
    CHECK(uos_event_wait(events,4,0)==2);
    uint64_t link=recv_direct((uint64_t)pid);
    CHECK(link && uos_port_accept((uint64_t)port,0)==(int64_t)link);
    CHECK(!uos_event_wait(events,4,0) && one(UOS_EVENT_CHANNEL,link,UOS_EVENT_READ|UOS_EVENT_WRITE,0)==UOS_EVENT_WRITE);
    for (unsigned i=0;i<UOS_IPC_DEPTH;++i) { CHECK(send_byte(link,(unsigned char)i,0)); }
    CHECK(!one(UOS_EVENT_CHANNEL,link,UOS_EVENT_WRITE,40));
    CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGUSR1) && one(UOS_EVENT_CHANNEL,link,UOS_EVENT_WRITE,WAIT_MS)==UOS_EVENT_WRITE);
    CHECK(one(UOS_EVENT_CHANNEL,link,UOS_EVENT_READ,WAIT_MS)==UOS_EVENT_READ &&
          one(UOS_EVENT_CHANNEL,link,UOS_EVENT_READ,0)==UOS_EVENT_READ && recv_byte(link,'r',0));
    CHECK(send_byte(link,'x',WAIT_MS) && one(UOS_EVENT_CHILD,(uint64_t)pid,UOS_EVENT_READ,WAIT_MS)==UOS_EVENT_READ);
    events[0]=watch(UOS_EVENT_CHANNEL,link,UOS_EVENT_READ|UOS_EVENT_WRITE);
    CHECK(uos_event_wait(events,4,0)==3 && events[0].revents==UOS_EVENT_HANGUP &&
          events[1].revents==UOS_EVENT_HANGUP && events[2].revents==UOS_EVENT_READ && !events[3].revents);
    CHECK(!uos_wait((uint64_t)pid) && one(UOS_EVENT_CHILD,(uint64_t)pid,UOS_EVENT_READ,0)==UOS_EVENT_HANGUP &&
          !uos_port_close((uint64_t)port));
    return 0;
}
static int signal_peer(uint64_t parent)
{
    CHECK(send_direct(parent,1) && one(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ,WAIT_MS)==UOS_EVENT_READ);
    CHECK(one(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ,0)==UOS_EVENT_READ && uos_signal_wait(0)==UOS_SIGUSR1);
    CHECK(!one(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ,0));
    CHECK(send_direct(parent,2) && one(UOS_EVENT_MAILBOX,parent,UOS_EVENT_READ,WAIT_MS)==UOS_EINTR &&
          uos_signal_wait(0)==UOS_SIGUSR1);
    return 0;
}
static int signal_checks(void)
{
    int64_t pid=start("signal",0);
    CHECK(pid>0);
    for (unsigned i=1;i<=2;++i) {
        CHECK(recv_direct((uint64_t)pid)==i && blocked((uint64_t)pid) &&
              !uos_signal_send((uint64_t)pid,UOS_SIGUSR1));
    }
    CHECK(!uos_wait((uint64_t)pid));
    return 0;
}
static int shared_peer(uint64_t parent)
{
    uint64_t token=recv_direct(parent);
    CHECK(token);
    int64_t address=uos_vm_map(token);
    CHECK(address>0);
    struct uos_event *row=(void *)(uintptr_t)address;
    *row=watch(UOS_EVENT_SIGNAL,0,UOS_EVENT_READ);
    CHECK(send_direct(parent,1) && uos_event_wait(row,1,WAIT_MS)==1 &&
          row->source==UOS_EVENT_SIGNAL && row->revents==UOS_EVENT_READ && uos_signal_wait(0)==UOS_SIGUSR1);
    CHECK(send_direct(parent,2) && uos_event_wait(row,1,WAIT_MS)==UOS_EFAULT);
    return 0;
}
static int shared_checks(void)
{
    int64_t pid=start("shared",0);
    uint64_t token;
    CHECK(pid>0);
    int64_t address=uos_vm_create((uint64_t)pid,1,UOS_READ|UOS_WRITE,&token);
    CHECK(address>0 && send_direct((uint64_t)pid,token));
    CHECK(recv_direct((uint64_t)pid)==1 && blocked((uint64_t)pid));
    struct uos_event *row=(void *)(uintptr_t)address;
    row->source=0;
    CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGUSR1));
    CHECK(recv_direct((uint64_t)pid)==2 && blocked((uint64_t)pid));
    CHECK(!uos_vm_revoke(token) && !uos_wait((uint64_t)pid) && !uos_vm_unmap(token));
    return 0;
}
static bool echo_byte(uint64_t link,unsigned char value)
{
    return send_byte(link,value,ECHO_MS) && recv_byte(link,value,ECHO_MS);
}
static int echo_peer(const char *name)
{
    int64_t link=uos_port_connect(name,0);
    CHECK(link>0);
    CHECK(echo_byte((uint64_t)link,'b') && !uos_port_close((uint64_t)link));
    return 0;
}
static int echo_checks(const char *name)
{
    int64_t link=uos_port_connect(name,0);
    CHECK(link>0 && echo_byte((uint64_t)link,'a'));
    int64_t pid=start("client",name);
    CHECK(pid>0 && !uos_wait((uint64_t)pid) && echo_byte((uint64_t)link,'c') && !uos_port_close((uint64_t)link));
    uos_puts("PASS: echo multiplexed two independent clients.\n");
    return 0;
}
int app_main(void)
{
    if (uos_argc()==3) {
        char mode[8],arg[21];
        CHECK(uos_arg(1,mode,sizeof(mode))>0 && uos_arg(2,arg,sizeof(arg))>0);
        if (same(mode,"echo")) { return echo_checks(arg); }
        if (same(mode,"client")) { return echo_peer(arg); }
        uint64_t parent=0;
        for (unsigned i=0;arg[i];++i) { parent=parent*10+(unsigned)(arg[i]-'0'); }
        if (same(mode,"peer")) { return channel_peer(parent); }
        if (same(mode,"signal")) { return signal_peer(parent); }
        if (same(mode,"shared")) { return shared_peer(parent); }
        return 2;
    }
    if (uos_argc()!=1) { return 2; }
    if (argument_checks() || channel_checks() || signal_checks() || shared_checks()) { return 1; }
    uos_puts("PASS: event waits, channel readiness, child status, signals and checked snapshots.\n");
    return 0;
}
