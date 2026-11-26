#include "user.h"

static const char app[]="/boot/LIMCHK.ELF",host[]="/etc/hostname";
static const char scratch[]="/tmp/lim40-data",output[]="/tmp/lim40-out";
enum { WAIT_MS=2000, INHERITED=UOS_ALLOW_SPAWN|UOS_ALLOW_FD_PASS };

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}
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
    uos_puts("FAIL: LIMCHK line ");uos_puts(decimal(text,line));uos_puts("\n");
    return 1;
}
#define CHECK(test) do { if (!(test)) { return failed(__LINE__); } } while (0)

static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}
static struct app_args command(const char *mode)
{
    struct app_args args={0};
    word(&args,app);word(&args,mode);
    return args;
}
static bool send_byte(uint64_t peer,unsigned char byte)
{
    struct uos_message message={.peer=peer,.bytes=1,.data={byte}};
    return !uos_ipc_send(&message,WAIT_MS);
}
static bool recv_byte(uint64_t peer,unsigned char byte)
{
    struct uos_message message;
    return !uos_ipc_recv(peer,&message,WAIT_MS) && !message.handle && message.bytes==1 && message.data[0]==byte;
}
static bool matches_file(const char *path,const char *expected)
{
    int64_t handle=uos_open(path,UOS_READ);
    if (handle<0) { return false; }
    char bytes[8]={0};
    int64_t size=uos_read((uint64_t)handle,bytes,sizeof(bytes)-1);
    return !uos_close((uint64_t)handle) && size==(int64_t)uos_length(expected) && same(bytes,expected);
}
static int denied_io(unsigned mask)
{
    struct uos_udp_peer udp={.ipv4={192,0,2,1},.port=9000};
    struct uos_tcp_peer tcp={.ipv4={192,0,2,1},.port=9000};
    CHECK(uos_policy_get(0)==(int64_t)mask && uos_policy_get(UINT64_MAX)==UOS_ECHILD);
    CHECK(uos_policy_restrict(UOS_POLICY_ALL)==UOS_EACCES && uos_policy_restrict(16)==UOS_ERROR);
    CHECK(uos_udp_open(&udp)==UOS_EACCES && uos_tcp_open(&tcp)==UOS_EACCES);
    CHECK(uos_open(output,UOS_WRITE|UOS_CREATE|UOS_EXCL)==UOS_EACCES &&
          uos_mkdir(output)==UOS_EACCES && uos_remove(output,0)==UOS_EACCES &&
          uos_replace(output,"x",1)==UOS_EACCES && uos_copy(host,output)==UOS_EACCES &&
          uos_rename(host,output)==UOS_EACCES && uos_publish(host,output)==UOS_EACCES &&
          uos_profile(UOS_PROFILE_SAVE,output)==UOS_EACCES && uos_sync()==UOS_EACCES);
    int64_t handle=uos_open(host,UOS_READ);
    CHECK(handle>0);
    uint64_t read=(uint64_t)handle;
    char byte;
    CHECK(uos_read(read,&byte,1)==1 && byte=='o' && uos_write_file(read,"x",1)==UOS_EACCES &&
          uos_pwrite(read,"x",1,0)==UOS_EACCES && uos_ftruncate(read,0)==UOS_EACCES &&
          uos_fd_set(read,UOS_APPEND,1)==UOS_EACCES && uos_fd_dup(read,UOS_WRITE,0)==UOS_EACCES);
    int64_t dup=uos_fd_dup(read,UOS_READ,0);
    CHECK(dup>0 && !uos_close((uint64_t)dup) && !uos_close(read));
    return 0;
}
static int locked(void)
{
    if (denied_io(0)) { return 1; }
    struct app_args args=command("inherit");
    CHECK(uos_spawn(&args,5)==UOS_EACCES && uos_spawn_ex(&args,5,0,0)==UOS_EACCES &&
          uos_spawn_limited(&args,5,0,0,0)==UOS_EACCES && uos_pipeline(&args,&args,5,0)==UOS_EACCES);
    uos_puts("PASS: mask 0 denies file mutations, sockets and child creation.\n");
    return 0;
}
static int drop_and_exec(void)
{
    int64_t writer=uos_open(scratch,UOS_WRITE|UOS_READ|UOS_CREATE|UOS_EXCL);
    int64_t reader=uos_open(host,UOS_READ);
    CHECK(writer>0 && reader>0 && uos_write_file((uint64_t)writer,"before",6)==6);
    CHECK(!uos_policy_restrict(INHERITED) && !uos_policy_restrict(INHERITED));
    struct uos_file_info info;
    CHECK(uos_fstat((uint64_t)writer,&info)==UOS_EBADF && !uos_fstat((uint64_t)reader,&info) &&
          !uos_close((uint64_t)reader) && matches_file(scratch,"before"));
    struct app_args args=command("inherit");
    CHECK(uos_spawn_limited(&args,5,0,0,UOS_POLICY_ALL)==UOS_EACCES);
    int64_t child=uos_spawn(&args,5);
    CHECK(child>0 && uos_policy_get((uint64_t)child)==INHERITED && !uos_wait((uint64_t)child));
    (void)uos_exec(&args);
    return failed(__LINE__);
}
static int drop_stdout(void)
{
    CHECK(uos_write("before",6)==6 && !uos_policy_restrict(UOS_POLICY_ALL&~UOS_ALLOW_FILE_WRITE) &&
          uos_write("x",1)==UOS_EACCES);
    return 0;
}
static int queued(uint64_t parent)
{
    const unsigned no_write=UOS_POLICY_ALL&~UOS_ALLOW_FILE_WRITE;
    CHECK(uos_signal_wait(WAIT_MS)==UOS_SIGUSR1 && !uos_policy_restrict(no_write));
    struct uos_message message;
    char byte;
    CHECK(!uos_ipc_recv(parent,&message,WAIT_MS) && message.handle && message.rights==UOS_READ &&
          uos_read(message.handle,&byte,1)==1 && byte=='o' && !uos_close(message.handle));
    CHECK(recv_byte(parent,'m') && send_byte(parent,'r') && uos_signal_wait(WAIT_MS)==UOS_SIGUSR1);
    CHECK(!uos_policy_restrict(no_write&~UOS_ALLOW_FD_PASS) && recv_byte(parent,'n'));
    int64_t read=uos_open(host,UOS_READ);
    CHECK(read>0);
    message=(struct uos_message){.peer=parent,.handle=(uint64_t)read,.rights=UOS_READ};
    CHECK(uos_ipc_send(&message,0)==UOS_EACCES && !uos_close((uint64_t)read) && send_byte(parent,'y'));
    CHECK(uos_signal_wait(WAIT_MS)==UOS_SIGUSR1);
    return 0;
}
static int transfer_checks(void)
{
    int64_t writer=uos_open(scratch,UOS_WRITE|UOS_CREATE|UOS_EXCL),reader=uos_open(host,UOS_READ);
    CHECK(writer>0 && reader>0);
    struct app_args args=command("queued");
    char text[21];
    word(&args,decimal(text,(uint64_t)uos_getpid()));
    int64_t child=uos_spawn(&args,10);
    CHECK(child>0);
    uint64_t pid=(uint64_t)child;
    struct uos_message w={.peer=pid,.handle=(uint64_t)writer,.rights=UOS_WRITE};
    struct uos_message r={.peer=pid,.handle=(uint64_t)reader,.rights=UOS_READ};
    CHECK(!uos_ipc_send(&w,0) && !uos_ipc_send(&r,0) && send_byte(pid,'m') &&
          !uos_signal_send(pid,UOS_SIGUSR1) && recv_byte(pid,'r'));
    CHECK(uos_ipc_send(&w,0)==UOS_EACCES && !uos_ipc_send(&r,0) && send_byte(pid,'n') &&
          !uos_signal_send(pid,UOS_SIGUSR1) && recv_byte(pid,'y'));
    CHECK(uos_ipc_send(&r,0)==UOS_EACCES && !uos_signal_send(pid,UOS_SIGUSR1) && !uos_wait(pid));
    CHECK(!uos_close((uint64_t)writer) && !uos_close((uint64_t)reader) && !uos_remove(scratch,0));
    return 0;
}
static int pipeline_checks(void)
{
    struct app_args left=command("pipeout"),right=command("pipein");
    struct uos_stdio io={.output="/tmp/lim40-out"};
    CHECK(!uos_policy_restrict(UOS_ALLOW_SPAWN));
    CHECK(uos_pipeline(&left,&right,2,&io)==UOS_EACCES && !uos_pipeline(&left,&right,2,0));
    struct uos_entry entry;
    CHECK(uos_stat(output,&entry)<0);
    uos_puts("PASS: pipeline inheritance permits pipe output and refuses file redirection.\n");
    return 0;
}
static int service(const char *name)
{
    if (denied_io(0)) { return 1; }
    struct app_args args={0};
    word(&args,"/boot/IPCECHO.ELF");word(&args,"serve");word(&args,name);
    (void)uos_exec(&args);
    return failed(__LINE__);
}
static int client(const char *name)
{
    int64_t channel=uos_port_connect(name,0),reader=uos_open(host,UOS_READ);
    CHECK(channel>0 && reader>0);
    uint64_t token=(uint64_t)channel;
    struct uos_message message={.handle=(uint64_t)reader,.rights=UOS_READ};
    CHECK(uos_channel_send(token,&message,WAIT_MS)==UOS_EACCES && !uos_close((uint64_t)reader));
    message=(struct uos_message){.bytes=1,.data={'s'}};
    CHECK(!uos_channel_send(token,&message,WAIT_MS) && !uos_channel_recv(token,&message,WAIT_MS) &&
          message.bytes==1 && message.data[0]=='s' && !message.handle && !uos_port_close(token));
    uos_puts("PASS: restricted service rejected a descriptor and echoed a message.\n");
    return 0;
}
int app_main(void)
{
    int64_t argc=uos_argc();
    if (argc>1) {
        char mode[16];
        CHECK(uos_arg(1,mode,sizeof(mode))>0);
        if (same(mode,"locked")) { return locked(); }
        if (same(mode,"inherit")) { return denied_io(INHERITED); }
        if (same(mode,"drop")) { return drop_and_exec(); }
        if (same(mode,"stdout")) { return drop_stdout(); }
        if (same(mode,"pipeline")) { return pipeline_checks(); }
        if (same(mode,"pipeout")) {
            CHECK(uos_policy_get(0)==UOS_ALLOW_SPAWN && uos_write("x",1)==1);return 0;
        }
        if (same(mode,"pipein")) {
            char byte;
            CHECK(uos_policy_get(0)==UOS_ALLOW_SPAWN && uos_stdin(&byte,1)==1 && byte=='x');return 0;
        }
        char arg[21];
        CHECK(argc==3 && uos_arg(2,arg,sizeof(arg))>0);
        if (same(mode,"service")) { return service(arg); }
        if (same(mode,"client")) { return client(arg); }
        if (same(mode,"queued")) {
            uint64_t parent=0;
            for (unsigned i=0;arg[i];++i) { parent=parent*10+(unsigned)(arg[i]-'0'); }
            return queued(parent);
        }
        return 2;
    }
    CHECK(uos_policy_get(0)==UOS_POLICY_ALL);
    struct app_args args=command("drop");
    int64_t child=uos_spawn(&args,10);
    CHECK(child>0 && !uos_wait((uint64_t)child) && matches_file(scratch,"before") && !uos_remove(scratch,0));
    args=command("stdout");
    struct uos_stdio io={.output="/tmp/lim40-out"};
    CHECK(uos_spawn_limited(&args,5,&io,0,0)==UOS_EACCES);
    struct uos_entry entry;
    CHECK(uos_stat(output,&entry)<0);
    child=uos_spawn_io(&args,5,&io);
    CHECK(child>0 && !uos_wait((uint64_t)child) && matches_file(output,"before") && !uos_remove(output,0));
    if (transfer_checks()) { return 1; }
    uos_puts("PASS: irreversible permissions, inheritance, exec and handle cleanup.\n");
    return 0;
}
