#include "user.h"

static const char app[]="/boot/CHANCHK.ELF";
static const char check_port[]="chan37";
static const char life_port[]="chan37-life";
static const char file[]="/tmp/chan37";

static char *decimal(char text[21],uint64_t value)
{
    unsigned at=20;
    text[at]=0;
    do { text[--at]=(char)('0'+value%10);value/=10; } while (value);
    return text+at;
}
static int failed(unsigned line)
{
    char text[21];
    uos_puts("FAIL: CHANCHK line ");uos_puts(decimal(text,line));uos_puts("\n");
    return 1;
}
#define CHECK(test) do { if (!(test)) { return failed(__LINE__); } } while (0)

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
static int64_t start(const char *mode)
{
    char pid[21];
    struct app_args args={0};
    word(&args,app);word(&args,mode);word(&args,decimal(pid,(uint64_t)uos_getpid()));
    return uos_spawn(&args,12);
}
static bool notify(uint64_t peer,uint64_t value)
{
    struct uos_message message={.peer=peer,.bytes=8};
    for (unsigned i=0;i<8;++i) { message.data[i]=(unsigned char)(value>>(i*8)); }
    return !uos_ipc_send(&message,2000);
}
static uint64_t await(uint64_t peer)
{
    struct uos_message message;
    if (uos_ipc_recv(peer,&message,2000) || message.bytes!=8 || message.handle) { return 0; }
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) { value|=(uint64_t)message.data[i]<<(i*8); }
    return value;
}
static bool send_byte(uint64_t link,unsigned char byte)
{
    struct uos_message message={.bytes=1,.data={byte}};
    return !uos_channel_send(link,&message,2000);
}
static bool receive_byte(uint64_t link,uint64_t peer,unsigned char byte)
{
    struct uos_message message;
    return !uos_channel_recv(link,&message,2000) && message.peer==peer && message.bytes==1 &&
        message.data[0]==byte && !message.handle;
}
static bool find_port(const char *name,struct uos_port_entry *out)
{
    struct uos_port_table table;
    if (uos_port_list(&table) || table.reserved || table.count>UOS_PORT_MAX) { return false; }
    for (unsigned i=0;i<table.count;++i) {
        if (same(name,table.entries[i].name)) { *out=table.entries[i];return true; }
    }
    return false;
}
static int registry(void)
{
    static const char *const names[]={"chan37-a","chan37-b","chan37-c","chan37-d"};
    uint64_t ports[UOS_PORT_MAX];
    struct uos_port_table table;
    CHECK(!uos_port_list(&table) && !table.count && !table.reserved);
    CHECK(uos_port_list(0)==UOS_EFAULT && uos_call2(UOS_SYS_PORT_LISTEN,1,1)==UOS_EFAULT);
    CHECK(uos_port_listen("bad name")==UOS_ERROR && uos_port_listen("1234567890123456")==UOS_ERROR);
    for (unsigned i=0;i<UOS_PORT_MAX;++i) {
        int64_t port=uos_port_listen(names[i]);
        CHECK(port>0);
        ports[i]=(uint64_t)port;
    }
    CHECK(uos_port_listen(names[0])==UOS_EEXIST && uos_port_listen("chan37-extra")==UOS_ENOSPC);
    CHECK(!uos_port_list(&table) && table.count==UOS_PORT_MAX && !table.reserved);
    for (unsigned i=0;i<UOS_PORT_MAX;++i) {
        CHECK(table.entries[i].pid==(uint64_t)uos_getpid() && same(table.entries[i].name,names[i]) &&
              !table.entries[i].pending && !table.entries[i].active);
    }
    CHECK(uos_port_connect(names[0],0)==UOS_EACCES && uos_port_accept(ports[0],0)==UOS_EAGAIN &&
          uos_port_accept(ports[0],30)==UOS_ETIMEDOUT);
    for (unsigned i=0;i<UOS_PORT_MAX;++i) { CHECK(!uos_port_close(ports[i])); }
    CHECK(uos_port_close(ports[0])==UOS_EPIPE && !uos_port_list(&table) && !table.count);
    return 0;
}
static int server(uint64_t parent)
{
    int64_t port=uos_port_listen(check_port);
    CHECK(port>0 && notify(parent,(uint64_t)port));
    uint64_t client=await(parent);
    CHECK(client);
    int64_t a=uos_port_accept((uint64_t)port,2000);
    int64_t b=uos_port_accept((uint64_t)port,2000);
    CHECK(a>0 && b>a && receive_byte((uint64_t)a,client,'s'));
    struct uos_port_entry entry;
    CHECK(find_port(check_port,&entry) && entry.active==2 && !entry.pending);
    CHECK(send_byte((uint64_t)b,'b') && send_byte((uint64_t)a,'a') && !uos_sleep_ms(700));
    struct uos_message message;
    CHECK(uos_ipc_recv(0,&message,0)==UOS_EAGAIN && uos_channel_recv((uint64_t)b,&message,0)==UOS_EAGAIN);
    CHECK(!uos_channel_recv((uint64_t)a,&message,2000) && message.peer==client &&
          message.handle && message.rights==UOS_READ && !message.bytes);
    uint64_t fd=message.handle;
    struct uos_fd_table descriptors;
    char byte;
    CHECK(!uos_fd_list(&descriptors) && descriptors.count==1 && descriptors.entries[0].token==fd &&
          descriptors.entries[0].descriptor_flags==UOS_CLOEXEC && uos_write_file(fd,"x",1)<0 &&
          uos_read(fd,&byte,1)==1 && byte=='b' && !uos_close(fd));
    for (unsigned i=1;i<=UOS_IPC_DEPTH;++i) { CHECK(receive_byte((uint64_t)a,client,(unsigned char)i)); }
    CHECK(send_byte((uint64_t)a,'d') && receive_byte((uint64_t)a,client,'q'));
    CHECK(!uos_port_close((uint64_t)port));
    return 0;
}
static int client(uint64_t parent)
{
    struct uos_port_entry entry;
    CHECK(find_port(check_port,&entry));
    uint64_t server_pid=entry.pid,channels[UOS_CHANNEL_MAX];
    struct uos_message message={.peer=server_pid};
    CHECK(uos_ipc_send(&message,0)==UOS_EPIPE && uos_port_connect(check_port,parent)==UOS_ENOENT);
    for (unsigned i=0;i<UOS_CHANNEL_MAX;++i) {
        int64_t link=uos_port_connect(check_port,server_pid);
        CHECK(link>0);
        channels[i]=(uint64_t)link;
    }
    CHECK(uos_port_connect(check_port,0)==UOS_ENOSPC && find_port(check_port,&entry) &&
          entry.pending==UOS_CHANNEL_MAX && !entry.active);
    for (unsigned i=2;i<UOS_CHANNEL_MAX;++i) { CHECK(!uos_port_close(channels[i])); }
    uint64_t a=channels[0],b=channels[1];
    message=(struct uos_message){.bytes=1,.data={'s'}};
    CHECK(uos_channel_send(a,&message,0)==UOS_EAGAIN && uos_channel_recv(channels[2],&message,0)==UOS_EPIPE);
    CHECK(notify(parent,a) && !uos_channel_send(a,&message,2000));
    CHECK(uos_channel_recv(a,0,0)==UOS_EFAULT && receive_byte(a,server_pid,'a') && receive_byte(b,server_pid,'b'));

    int64_t handle=uos_open(file,UOS_CREATE|UOS_EXCL|UOS_READ|UOS_WRITE);
    CHECK(handle>0 && uos_write_file((uint64_t)handle,"abc",3)==3 && uos_seek((uint64_t)handle,1,UOS_SEEK_SET)==1);
    struct uos_message fd={.handle=(uint64_t)handle,.rights=UOS_READ};
    CHECK(!uos_channel_send(a,&fd,2000));
    for (unsigned i=1;i<UOS_IPC_DEPTH;++i) { CHECK(send_byte(a,(unsigned char)i)); }
    message.data[0]=UOS_IPC_DEPTH;
    CHECK(uos_channel_send(a,&message,0)==UOS_EAGAIN && uos_channel_send(a,&message,40)==UOS_ETIMEDOUT);
    CHECK(!uos_channel_send(a,&message,2000) && receive_byte(a,server_pid,'d'));
    struct uos_file_info stat;
    CHECK(!uos_fstat((uint64_t)handle,&stat) && stat.offset==2 && !uos_channel_send(b,&fd,2000) &&
          send_byte(a,'q') && uos_channel_recv(b,&message,2000)==UOS_EPIPE);
    CHECK(uos_port_connect(check_port,0)==UOS_ENOENT && uos_port_close(a)==UOS_EPIPE &&
          !uos_close((uint64_t)handle) && !uos_remove(file,0));
    return 0;
}
static int sibling_checks(void)
{
    int64_t s=start("server");
    CHECK(s>0);
    uint64_t port=await((uint64_t)s);
    CHECK(port && uos_port_accept(port,0)==UOS_EPIPE && uos_port_close(port)==UOS_EPIPE);
    int64_t c=start("client");
    CHECK(c>0);
    uint64_t channel=await((uint64_t)c);
    struct uos_message message={0};
    CHECK(channel && uos_port_close(channel)==UOS_EPIPE &&
          uos_channel_send(channel,&message,0)==UOS_EPIPE && uos_channel_recv(channel,&message,0)==UOS_EPIPE);
    CHECK(notify((uint64_t)s,(uint64_t)c) && !uos_wait((uint64_t)c) && !uos_wait((uint64_t)s));
    return 0;
}
static int endpoint_lifecycle(uint64_t parent,bool replace)
{
    int64_t port=uos_port_listen(life_port);
    CHECK(port>0 && notify(parent,(uint64_t)port));
    int64_t link=uos_port_accept((uint64_t)port,2000);
    CHECK(link>0 && receive_byte((uint64_t)link,parent,'x'));
    if (replace) {
        struct app_args args={0};
        word(&args,"/boot/NONE.ELF");
        CHECK(uos_exec(&args)==UOS_ERROR);
    }
    CHECK(send_byte((uint64_t)link,'r') && receive_byte((uint64_t)link,parent,'g'));
    if (!replace) { return 0; }
    struct app_args args={0};
    word(&args,"/boot/HELLO.ELF");
    (void)uos_exec(&args);
    return failed(__LINE__);
}
static int lifecycle_checks(void)
{
    for (unsigned i=0;i<2;++i) {
        int64_t pid=start(i ? "exec" : "leave");
        CHECK(pid>0 && await((uint64_t)pid));
        int64_t link=uos_port_connect(life_port,(uint64_t)pid);
        CHECK(link>0 && send_byte((uint64_t)link,'x') && receive_byte((uint64_t)link,(uint64_t)pid,'r'));
        struct uos_message reply;
        CHECK(send_byte((uint64_t)link,'g') && uos_channel_recv((uint64_t)link,&reply,2000)==UOS_EPIPE &&
              !uos_wait((uint64_t)pid) && uos_port_connect(life_port,0)==UOS_ENOENT);
    }
    struct uos_port_table table;
    CHECK(!uos_port_list(&table) && !table.count);
    return 0;
}
static int join_port(uint64_t parent)
{
    int64_t link=uos_port_connect(life_port,parent);
    CHECK(link>0);
    struct uos_message message;
    (void)uos_channel_recv((uint64_t)link,&message,2000);
    return failed(__LINE__);
}
static int interrupted_accept(uint64_t parent)
{
    int64_t port=uos_port_listen(life_port);
    CHECK(port>0 && notify(parent,(uint64_t)port));
    CHECK(uos_port_accept((uint64_t)port,2000)==UOS_EINTR && uos_signal_wait(0)==UOS_SIGUSR1);
    return 0;
}
static int cancellation_checks(void)
{
    int64_t port=uos_port_listen(life_port);
    CHECK(port>0);
    int64_t pid=start("join");
    CHECK(pid>0);
    int64_t link=uos_port_accept((uint64_t)port,2000);
    struct uos_port_entry entry;
    CHECK(link>0 && find_port(life_port,&entry) && entry.active==1 && !entry.pending);
    CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGTERM) && find_port(life_port,&entry) &&
          !entry.active && !entry.pending);
    struct uos_message message;
    CHECK(uos_channel_recv((uint64_t)link,&message,0)==UOS_EPIPE &&
          uos_wait((uint64_t)pid)==143 && !uos_port_close((uint64_t)port));

    pid=start("signal");
    CHECK(pid>0 && await((uint64_t)pid));
    struct uos_child_info child;
    for (unsigned i=0;i<100;++i) {
        CHECK(!uos_child_poll((uint64_t)pid,&child) && !child.finished);
        if (child.state==UOS_PROCESS_IPC_RECV) { break; }
        CHECK(!uos_sleep_ms(10));
    }
    CHECK(child.state==UOS_PROCESS_IPC_RECV && !uos_signal_send((uint64_t)pid,UOS_SIGUSR1) &&
          !uos_wait((uint64_t)pid) && uos_port_connect(life_port,0)==UOS_ENOENT);
    return 0;
}
int app_main(void)
{
    if (uos_argc()==3) {
        char mode[8],text[21];
        CHECK(uos_arg(1,mode,sizeof(mode))>0 && uos_arg(2,text,sizeof(text))>0);
        uint64_t parent=0;
        for (unsigned i=0;text[i];++i) { parent=parent*10+(unsigned)(text[i]-'0'); }
        if (same(mode,"server")) { return server(parent); }
        if (same(mode,"client")) { return client(parent); }
        if (same(mode,"join")) { return join_port(parent); }
        if (same(mode,"signal")) { return interrupted_accept(parent); }
        return endpoint_lifecycle(parent,same(mode,"exec"));
    }
    if (registry() || sibling_checks() || lifecycle_checks() || cancellation_checks()) { return 1; }
    uos_puts("PASS: named listeners, sibling channels, timed queues, descriptor rights and teardown.\n");
    return 0;
}
