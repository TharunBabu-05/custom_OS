#include "user.h"

static const char app[]="/boot/CORECHK.ELF";
static const char data_path[]="/tmp/core30-file";
static const char service_started[]="/tmp/core30-started";
static const char service_beat[]="/tmp/core30-beat";

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

static void decimal(char out[24],uint64_t n)
{
    char reversed[24];
    unsigned used=0;
    do { reversed[used++]=(char)('0'+n%10);n/=10; } while (n);
    for (unsigned i=0;i<used;++i) { out[i]=reversed[used-i-1]; }
    out[used]=0;
}

static int failed(unsigned line)
{
    char number[24];
    decimal(number,line);
    uos_puts("FAIL: CORECHK line ");uos_puts(number);uos_puts("\n");
    return 1;
}
#define CHECK(c) do { if (!(c)) { return failed(__LINE__); } } while (0)

static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}

static struct app_args command(const char *path,const char *mode,uint64_t peer)
{
    struct app_args args={0};
    word(&args,path);
    if (mode) {
        char pid[24];
        decimal(pid,peer);
        word(&args,mode);word(&args,pid);
    }
    return args;
}

static int64_t spawn(const char *path,const char *mode,unsigned seconds)
{
    struct app_args args=command(path,mode,(uint64_t)uos_getpid());
    return uos_spawn(&args,seconds);
}

static int64_t send_byte(uint64_t peer,unsigned char byte)
{
    struct uos_message message={.peer=peer,.bytes=1,.data={byte}};
    return uos_ipc_send(&message,2000);
}

static bool receive_byte(uint64_t peer,unsigned char byte)
{
    struct uos_message message;
    return !uos_ipc_recv(peer,&message,2000) && message.peer==peer &&
        message.bytes==1 && message.data[0]==byte && !message.handle;
}

static void put_token(struct uos_message *message,uint64_t token)
{
    message->bytes=8;
    for (unsigned i=0;i<8;++i) { message->data[i]=(unsigned char)(token>>(8*i)); }
}

static uint64_t get_token(const struct uos_message *message)
{
    uint64_t token=0;
    for (unsigned i=0;i<8;++i) { token|=(uint64_t)message->data[i]<<(8*i); }
    return token;
}

static uint64_t parse_decimal(const char *text)
{
    uint64_t value=0;
    for (;*text;++text) { value=value*10+(unsigned)(*text-'0'); }
    return value;
}

static bool await_state(uint64_t pid,unsigned state)
{
    struct uos_child_info child;
    for (unsigned i=0;i<300;++i) {
        if (uos_child_poll(pid,&child)) { return false; }
        if (child.state==state) { return true; }
        if (child.finished || uos_sleep_ms(10)) { return false; }
    }
    return false;
}

static bool stopped(uint64_t pid,unsigned reason,unsigned code,unsigned signal)
{
    struct uos_fault_info exit;
    for (unsigned i=0;i<300;++i) {
        int64_t r=uos_child_fault(pid,&exit);
        if (!r) {
            return exit.pid==pid && exit.reason==reason && exit.code==code && exit.signal==signal &&
                (reason==UOS_STOP_FAULT ? exit.esr && exit.pc && exit.flags==UOS_FAULT_FAR_VALID :
                 !exit.esr && !exit.pc && !exit.flags);
        }
        if (r!=UOS_EAGAIN || uos_sleep_ms(10)) { return false; }
    }
    return false;
}

static int transfer_receiver(uint64_t peer)
{
    struct uos_message received;
    CHECK(uos_sleep_ms(300)==0);
    for (unsigned i=0;i<UOS_IPC_DEPTH;++i) { CHECK(receive_byte(peer,(unsigned char)i)); }
    CHECK(!uos_ipc_recv(peer,&received,2000) && received.peer==peer && received.bytes==8 &&
          received.handle && received.rights==UOS_READ);
    uint64_t fd=received.handle,token=get_token(&received);
    struct uos_file_info stat;
    struct uos_fd_table descriptors;
    CHECK(!uos_fstat(fd,&stat) && stat.flags==UOS_READ && stat.offset==1);
    CHECK(!uos_fd_list(&descriptors) && descriptors.count==1 &&
          descriptors.entries[0].descriptor_flags==UOS_CLOEXEC);
    CHECK(uos_write_file(fd,"bad",3)<0 && uos_fd_dup(fd,UOS_WRITE,0)<0);
    int64_t address=uos_vm_map(token);
    CHECK(address>0 && uos_vm_map(token)==UOS_ENOENT);
    volatile unsigned char *view=(void *)(uintptr_t)address;
    CHECK(view[0]==17 && view[31*4096+4095]==29);
    CHECK(uos_read(fd,(void *)(uintptr_t)address,1)==UOS_EFAULT);
    char ch;
    CHECK(uos_read(fd,&ch,1)==1 && ch=='b');
    CHECK(!send_byte(peer,'r') && receive_byte(peer,'u'));
    CHECK(view[31*4096+4095]==29 && !uos_vm_unmap(token) && uos_vm_map(token)==UOS_ENOENT);
    CHECK(!uos_close(fd) && !send_byte(peer,'d'));
    return 0;
}

static int transfer_parent(void)
{
    int64_t pid=spawn(app,"receive",8);
    CHECK(pid>0);
    uint64_t token=0,other=0;
    int64_t address=uos_vm_create((uint64_t)pid,32,UOS_READ,&token);
    CHECK(address>0 && token);
    CHECK(uos_vm_create(0,17,0,&other)==UOS_ENOSPC && !other);
    volatile unsigned char *view=(void *)(uintptr_t)address;
    for (unsigned i=0;i<32;++i) { CHECK(view[i*4096]==0 && view[i*4096+4095]==0); }
    view[0]=17;view[31*4096+4095]=29;
    int64_t fd=uos_open(data_path,UOS_READ|UOS_WRITE|UOS_CREATE|UOS_EXCL);
    CHECK(fd>0 && uos_write_file((uint64_t)fd,"abc",3)==3 && uos_seek((uint64_t)fd,1,UOS_SEEK_SET)==1);
    for (unsigned i=0;i<UOS_IPC_DEPTH;++i) { CHECK(!send_byte((uint64_t)pid,(unsigned char)i)); }
    struct uos_message message={.peer=(uint64_t)pid,.handle=(uint64_t)fd,.rights=UOS_READ};
    put_token(&message,token);
    CHECK(uos_ipc_send(&message,0)==UOS_EAGAIN);
    CHECK(uos_ipc_send(&message,40)==UOS_ETIMEDOUT);
    CHECK(!uos_ipc_send(&message,2000));
    CHECK(uos_ipc_recv((uint64_t)pid,(void *)1,0)==UOS_EFAULT && receive_byte((uint64_t)pid,'r'));
    struct uos_file_info stat;
    CHECK(!uos_fstat((uint64_t)fd,&stat) && stat.offset==2);
    CHECK(!uos_vm_unmap(token) && uos_vm_unmap(token)==UOS_ENOENT);
    CHECK(!send_byte((uint64_t)pid,'u') && receive_byte((uint64_t)pid,'d') && !uos_wait((uint64_t)pid));
    CHECK(!uos_close((uint64_t)fd) && !uos_remove(data_path,0));
    return 0;
}

static int signal_receiver(uint64_t peer)
{
    struct uos_message message;
    CHECK(uos_ipc_recv(peer,&message,2000)==UOS_EINTR);
    CHECK(uos_signal_wait(0)==UOS_SIGUSR1 && !send_byte(peer,'s'));
    CHECK(uos_signal_wait(2000)==UOS_SIGUSR1);
    CHECK(uos_signal_wait(20)==UOS_ETIMEDOUT);
    return 0;
}

static int signals_and_faults(void)
{
    int64_t pid=spawn(app,"signal",8);
    CHECK(pid>0 && await_state((uint64_t)pid,UOS_PROCESS_IPC_RECV));
    CHECK(uos_signal_send((uint64_t)uos_getpid(),UOS_SIGUSR1)==UOS_ECHILD);
    CHECK(uos_child_fault((uint64_t)pid,(void *)1)==UOS_EFAULT);
    struct uos_fault_info info;
    CHECK(uos_child_fault((uint64_t)pid,&info)==UOS_EAGAIN);
    CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGUSR1) && receive_byte((uint64_t)pid,'s'));
    CHECK(await_state((uint64_t)pid,UOS_PROCESS_SIGNAL_WAIT) &&
          !uos_signal_send((uint64_t)pid,UOS_SIGUSR1) && !uos_wait((uint64_t)pid));
    CHECK(uos_child_fault((uint64_t)pid,&info)==UOS_ECHILD);

    pid=spawn(app,"signal",8);
    CHECK(pid>0 && await_state((uint64_t)pid,UOS_PROCESS_IPC_RECV));
    CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGTERM) && stopped((uint64_t)pid,UOS_STOP_SIGNAL,143,UOS_SIGTERM));
    CHECK(uos_wait((uint64_t)pid)==143);
    pid=spawn("/boot/FAULT.ELF",NULL,3);
    CHECK(pid>0 && stopped((uint64_t)pid,UOS_STOP_FAULT,125,0) && uos_wait((uint64_t)pid)==125);
    pid=spawn("/boot/SPIN.ELF",NULL,1);
    CHECK(pid>0 && stopped((uint64_t)pid,UOS_STOP_TIMEOUT,124,0) && uos_wait((uint64_t)pid)==124);
    return 0;
}

static int replace_receiver(uint64_t peer)
{
    struct uos_message message;
    CHECK(!uos_ipc_recv(peer,&message,2000) && message.bytes==8 && !message.handle);
    uint64_t token=get_token(&message);
    int64_t address=uos_vm_map(token);
    CHECK(address>0);
    volatile unsigned char *view=(void *)(uintptr_t)address;
    CHECK(view[0]==19);
    view[4096]=23;
    CHECK(!send_byte(peer,'m') && uos_signal_wait(2000)==UOS_SIGUSR1);
    struct app_args args=command("/boot/NONE.ELF",NULL,0);
    CHECK(uos_exec(&args)<0 && view[0]==19 && view[4096]==23);
    args=command(app,"after-exec",peer);
    char text[24];
    decimal(text,token);word(&args,text);
    decimal(text,(uint64_t)address);word(&args,text);
    (void)uos_exec(&args);
    return failed(__LINE__);
}

static int discarded_handles(void)
{
    for (unsigned exec=0;exec<2;++exec) {
        int64_t pid=spawn(app,exec ? "replace" : "close",5);
        int64_t fd=uos_open(data_path,UOS_READ|UOS_WRITE|UOS_CREATE|UOS_EXCL);
        CHECK(pid>0 && fd>0);
        uint64_t token=0;
        if (exec) {
            int64_t address=uos_vm_create((uint64_t)pid,2,UOS_READ|UOS_WRITE,&token);
            CHECK(address>0 && token);
            volatile unsigned char *view=(void *)(uintptr_t)address;
            view[0]=19;
            struct uos_message grant={.peer=(uint64_t)pid};
            put_token(&grant,token);
            CHECK(!uos_ipc_send(&grant,2000) && receive_byte((uint64_t)pid,'m') && view[4096]==23);
        }
        struct uos_message send={.peer=(uint64_t)pid,.handle=(uint64_t)fd,.rights=UOS_READ},received;
        CHECK(!uos_ipc_send(&send,0) && !uos_close((uint64_t)fd) && uos_remove(data_path,0)<0);
        if (exec) {
            CHECK(!uos_signal_send((uint64_t)pid,UOS_SIGUSR1) && receive_byte((uint64_t)pid,'e'));
            CHECK(!uos_vm_unmap(token));
        } else {
            CHECK(uos_ipc_recv((uint64_t)pid,&received,2000)==UOS_EPIPE);
        }
        CHECK(!uos_remove(data_path,0) && !uos_wait((uint64_t)pid));
    }
    return 0;
}

static int run_service(void)
{
    struct uos_entry info;
    if (uos_stat(service_started,&info)) {
        CHECK(uos_replace(service_started,"1",1)==1);
        return 31;
    }
    int64_t retained=uos_open(service_started,UOS_READ);
    CHECK(retained>0);
    uint64_t token;
    CHECK(uos_vm_create(0,17,0,&token)>0 && token);
    for (uint64_t n=1;;++n) {
        char beat[24];
        decimal(beat,n);
        size_t bytes=uos_length(beat);
        beat[bytes++]='\n';
        CHECK(uos_replace(service_beat,beat,bytes)==(int64_t)bytes);
        CHECK(!uos_sleep_ms(500));
    }
}

int app_main(void)
{
    if (uos_argc()==1) {
        (void)uos_remove(data_path,0);
        if (transfer_parent() || signals_and_faults() || discarded_handles()) { return 1; }
        uos_puts("PASS: core IPC, descriptor transfer, blocking, signals, fault reports and virtual memory.\n");
        return 0;
    }
    char mode[20],pid[24];
    CHECK(uos_arg(1,mode,sizeof(mode))>0);
    if (same(mode,"service")) { return run_service(); }
    CHECK(uos_arg(2,pid,sizeof(pid))>0);
    uint64_t peer=parse_decimal(pid);
    if (same(mode,"receive")) { return transfer_receiver(peer); }
    if (same(mode,"signal")) { return signal_receiver(peer); }
    if (same(mode,"replace")) { return replace_receiver(peer); }
    if (same(mode,"close")) { return (int)uos_sleep_ms(300); }
    if (same(mode,"after-exec")) {
        char token[24],address[24];
        CHECK(uos_arg(3,token,sizeof(token))>0 && uos_arg(4,address,sizeof(address))>0);
        struct uos_message message;
        CHECK(uos_vm_map(parse_decimal(token))==UOS_ENOENT &&
              uos_ipc_recv(0,(void *)(uintptr_t)parse_decimal(address),0)==UOS_EFAULT);
        CHECK(uos_ipc_recv(0,&message,0)==UOS_EAGAIN && !send_byte(peer,'e'));
        return 0;
    }
    return failed(__LINE__);
}
