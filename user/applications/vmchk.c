#include "user.h"

enum { PAGE=4096, LARGE_BYTES=16384, READ_WRITE=UOS_READ|UOS_WRITE };
static const char large[]="/boot/LARGE.BIN";
static const char app[]="/boot/VMCHK.ELF";

static int failed(unsigned line)
{
    char number[12];
    unsigned at=sizeof(number)-1;
    number[at]=0;
    do { number[--at]=(char)('0'+line%10);line/=10; } while (line);
    uos_puts("FAIL: VMCHK line ");uos_puts(number+at);uos_puts("\n");
    return 1;
}
#define CHECK(test) do { if (!(test)) { return failed(__LINE__); } } while (0)

static void word(struct app_args *a,const char *s)
{
    a->offset[a->count++]=a->used;
    do { a->bytes[a->used++]=*s; } while (*s++);
}
static struct app_args command(const char *mode)
{
    struct app_args a={0};
    word(&a,app);word(&a,mode);
    return a;
}
static int64_t start(const char *mode)
{
    struct app_args a=command(mode);
    return uos_spawn(&a,8);
}
static unsigned char pattern(unsigned offset) { return (unsigned char)(offset*29+7); }

static int file_checks(void)
{
    int64_t h=uos_open(large,UOS_READ);
    uint64_t token=0,unused=0;
    CHECK(h>0 && uos_seek((uint64_t)h,17,UOS_SEEK_SET)==17);
    CHECK(uos_vm_file((uint64_t)h,LARGE_BYTES+1,1,UOS_READ,&unused)==UOS_ERROR && !unused);
    CHECK(uos_vm_file((uint64_t)h,0,1,UOS_WRITE,&unused)==UOS_ERROR && !unused);
    CHECK(uos_vm_file((uint64_t)h,0,1,UOS_READ,0)==UOS_EFAULT);
    int64_t addr=uos_vm_file((uint64_t)h,4093,5000,UOS_READ,&token);
    CHECK(addr>0 && token);
    unsigned char *view=(void *)(uintptr_t)addr;
    for (unsigned i=0;i<PAGE*2;++i) { CHECK(view[i]==(i<5000 ? pattern(4093+i) : 0)); }
    struct uos_vm_info info;
    CHECK(!uos_vm_info(0,&info) && info.count==1 && info.pages==2 && info.entries[0].file_offset==4093 &&
          info.entries[0].bytes==5000 && info.entries[0].flags==UOS_VM_FILE_BACKED && info.entries[0].address==(uint64_t)addr &&
          info.entries[0].token==token && info.entries[0].rights==UOS_READ && info.entries[0].ceiling==UOS_READ);
    CHECK(uos_vm_protect(token,READ_WRITE)==UOS_EACCES && uos_pread((uint64_t)h,view,1,0)==UOS_EFAULT && !uos_vm_unmap(token));
    addr=uos_vm_file((uint64_t)h,LARGE_BYTES-3,PAGE+5,READ_WRITE,&token);
    CHECK(addr>0);
    view=(void *)(uintptr_t)addr;
    for (unsigned i=0;i<PAGE*2;++i) { CHECK(view[i]==(i<3 ? pattern(LARGE_BYTES-3+i) : 0)); }
    view[0]='x';view[PAGE]='y';
    CHECK(!uos_vm_protect(token,UOS_READ) && !uos_vm_info(0,&info) && info.entries[0].rights==UOS_READ &&
          info.entries[0].ceiling==READ_WRITE && uos_pread((uint64_t)h,view,1,0)==UOS_EFAULT);
    struct uos_file_info file;
    unsigned char before=0;
    CHECK(uos_pread((uint64_t)h,&before,1,LARGE_BYTES-3)==1 && before==pattern(LARGE_BYTES-3) &&
          !uos_fstat((uint64_t)h,&file) && file.offset==17 && !uos_close((uint64_t)h));
    CHECK(uos_vm_file((uint64_t)h,0,1,UOS_READ,&unused)==UOS_EBADF && !unused);
    CHECK(!uos_vm_protect(token,READ_WRITE) && view[0]=='x' && view[PAGE]=='y');
    view[PAGE]='z';
    CHECK(!uos_vm_unmap(token) && uos_write(view,1)==UOS_EFAULT);

    const char *path="/tmp/vmchk36";
    int64_t writer=uos_open(path,UOS_WRITE|UOS_CREATE|UOS_EXCL);
    CHECK(writer>0 && uos_vm_file((uint64_t)writer,0,1,UOS_READ,&unused)==UOS_EACCES && !unused);
    CHECK(uos_write_file((uint64_t)writer,"ram",3)==3 && !uos_close((uint64_t)writer));
    h=uos_open(path,UOS_READ);
    CHECK(h>0);
    addr=uos_vm_file((uint64_t)h,0,PAGE*UOS_VM_MAX_PAGES,UOS_READ,&token);
    CHECK(addr>0 && !uos_close((uint64_t)h) && !uos_remove(path,0));
    view=(void *)(uintptr_t)addr;
    CHECK(view[0]=='r' && view[2]=='m' && !view[3] && !view[PAGE*UOS_VM_MAX_PAGES-1] &&
          uos_vm_create(0,17,0,&unused)==UOS_ENOSPC && !unused && !uos_vm_unmap(token));
    return 0;
}

static uint64_t parent_pid(void)
{
    struct uos_process_table table;
    if (uos_processes(&table) || table.count>UOS_PROCESS_MAX) { return 0; }
    for (unsigned i=0;i<table.count;++i) {
        if (table.entries[i].flags&UOS_PROCESS_SELF) { return table.entries[i].parent_pid; }
    }
    return 0;
}
static int64_t send(uint64_t peer,uint64_t value)
{
    struct uos_message message={.peer=peer,.bytes=8};
    for (unsigned i=0;i<8;++i) { message.data[i]=(unsigned char)(value>>(8*i)); }
    return uos_ipc_send(&message,2000);
}
static uint64_t receive(uint64_t peer)
{
    struct uos_message message;
    if (uos_ipc_recv(peer,&message,2000) || message.bytes!=8 || message.peer!=peer || message.handle) { return 0; }
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) { value|=(uint64_t)message.data[i]<<(8*i); }
    return value;
}

static int peer_check(char mode)
{
    uint64_t parent=parent_pid(),token=receive(parent);
    CHECK(parent && token);
    if (mode=='p') { return uos_vm_map(token)==UOS_ENOENT ? 0 : failed(__LINE__); }
    int64_t addr=uos_vm_map(token);
    CHECK(addr>0 && uos_vm_revoke(token)==UOS_ENOENT);
    if (mode=='b') {
        CHECK(!send(parent,1));
        CHECK(uos_ipc_recv(parent,(struct uos_message *)(uintptr_t)addr,2000)==UOS_EFAULT);
        return 0;
    }
    CHECK(uos_vm_protect(token,READ_WRITE)==UOS_EACCES && !send(parent,1) && receive(parent)==1);
    CHECK(uos_vm_map(token)==UOS_ENOENT && uos_vm_protect(token,UOS_READ)==UOS_ENOENT);
    return *(const volatile unsigned char *)(uintptr_t)addr;
}

static int share(const char *mode,unsigned rights)
{
    int64_t child=start(mode);
    CHECK(child>0);
    uint64_t pid=(uint64_t)child,token;
    int64_t addr=uos_vm_create(pid,1,rights,&token);
    CHECK(addr>0);
    unsigned char *view=(void *)(uintptr_t)addr;view[0]=73;
    struct uos_vm_info info;
    if (mode[0]=='p') {
        CHECK(!uos_vm_revoke(token) && !send(pid,token));
    } else {
        CHECK(!send(pid,token) && receive(pid)==1);
        CHECK(!uos_vm_info(pid,&info) && info.count==1 && info.entries[0].rights==rights &&
              info.entries[0].flags==(UOS_VM_SHARED|UOS_VM_PEER) && !info.entries[0].token && !info.entries[0].address);
        if (mode[0]=='b') {
            struct uos_child_info child_info;
            do { CHECK(!uos_child_poll(pid,&child_info) && !child_info.finished); }
            while (child_info.state!=UOS_PROCESS_IPC_RECV && !uos_yield());
            CHECK(child_info.state==UOS_PROCESS_IPC_RECV);
        }
        CHECK(!uos_vm_revoke(token) && !uos_vm_info(pid,&info) && !info.count);
        if (mode[0]=='r') { CHECK(!send(pid,1)); }
    }
    CHECK(uos_wait(pid)==(mode[0]=='r' ? 125 : 0) && view[0]==73 &&
          uos_vm_revoke(token)==UOS_ENOENT && !uos_vm_unmap(token));
    return 0;
}

static int lifecycle(char mode)
{
    struct uos_vm_info info;
    if (mode=='c') { return !uos_vm_info(0,&info) && !info.count ? 0 : failed(__LINE__); }
    int64_t h=uos_open(large,UOS_READ);
    uint64_t token;
    CHECK(h>0);
    int64_t addr=uos_vm_file((uint64_t)h,0,1,READ_WRITE,&token);
    CHECK(addr>0 && !uos_close((uint64_t)h));
    if (mode=='f') {
        CHECK(!uos_vm_protect(token,UOS_READ));
        *(volatile unsigned char *)(uintptr_t)addr='x';
        return failed(__LINE__);
    }
    if (mode=='h') { return uos_signal_wait(10000)<0; }
    struct app_args next=command("clear");
    (void)uos_exec(&next);
    return failed(__LINE__);
}

int app_main(void)
{
    if (uos_argc()>1) {
        char mode[16];
        CHECK(uos_arg(1,mode,sizeof(mode))>0);
        if (mode[0]=='p' || mode[0]=='r' || mode[0]=='b') { return peer_check(mode[0]); }
        return lifecycle(mode[0]);
    }
    if (file_checks() || share("pending",UOS_READ) || share("reader",UOS_READ) || share("blocked",READ_WRITE)) { return 1; }
    int64_t child=start("fault");
    CHECK(child>0 && uos_wait((uint64_t)child)==125);
    child=start("exec");
    CHECK(child>0 && !uos_wait((uint64_t)child));
    struct uos_vm_info info;
    CHECK(uos_vm_info(0,0)==UOS_EFAULT && !uos_vm_info(0,&info) && !info.count && !info.pages);
    uos_puts("PASS: file windows, private writes, protection, shared revocation, VM inventory and cleanup.\n");
    return 0;
}
