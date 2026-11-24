#include "user.h"
#include <stdbool.h>

static const char scratch[]="/tmp/fddup";
static const char sample[]="/tmp/fddup/file";

static bool exercise(uint64_t initial)
{
    int64_t duplicate=uos_fd_dup(initial,0,0);
    int64_t reader=uos_fd_dup(initial,UOS_READ,UOS_CLOEXEC);
    char byte;
    struct uos_file_info info;
    if (duplicate<=0 || reader<=0 || duplicate==reader || (uint64_t)duplicate==initial) { return false; }
    if (uos_write_file(initial,"abc",3)!=3 || uos_seek((uint64_t)duplicate,0,UOS_SEEK_SET)!=0 ||
        uos_read((uint64_t)reader,&byte,1)!=1 || byte!='a' ||
        uos_read(initial,&byte,1)!=1 || byte!='b') { return false; }
    if (uos_fd_dup((uint64_t)reader,UOS_WRITE,0)!=UOS_ERROR ||
        uos_pwrite((uint64_t)reader,"x",1,0)!=UOS_ERROR ||
        uos_ftruncate((uint64_t)reader,0)!=UOS_ERROR ||
        uos_fd_set((uint64_t)reader,UOS_APPEND,1)!=UOS_ERROR ||
        uos_fd_set(initial,UOS_APPEND,2)!=UOS_ERROR) { return false; }
    if (uos_fd_set((uint64_t)duplicate,UOS_APPEND,1) ||
        uos_fstat(initial,&info) || info.flags!=(UOS_READ|UOS_WRITE|UOS_APPEND) ||
        uos_write_file(initial,"!",1)!=1 ||
        uos_fd_set(initial,UOS_APPEND,0) ||
        uos_seek((uint64_t)duplicate,1,UOS_SEEK_SET)!=1 ||
        uos_write_file(initial,"X",1)!=1) { return false; }
    return !uos_fd_set(initial,UOS_CLOEXEC,0) && !uos_fd_set(initial,UOS_CLOEXEC,1);
}

static void exec_args(struct app_args *args,const char *path)
{
    const char phase[]="post";
    size_t bytes=uos_length(path)+1;
    *args=(struct app_args){.count=2,.used=(unsigned)(bytes+sizeof(phase)),.offset={0,(unsigned)bytes}};
    for (size_t i=0;i<bytes;++i) { args->bytes[i]=path[i]; }
    for (size_t i=0;i<sizeof(phase);++i) { args->bytes[bytes+i]=phase[i]; }
}

static bool check_before_exec(void)
{
    struct uos_fd_table table;
    if (uos_fd_list(NULL)!=UOS_EFAULT || uos_fd_list(&table) ||
        table.count!=3 || table.reserved) { return false; }
    unsigned marked=0,readers=0;
    for (unsigned i=0;i<table.count;++i) {
        const struct uos_fd_entry *entry=&table.entries[i];
        if (!entry->token || entry->bytes!=4 || entry->offset!=2) { return false; }
        marked+=entry->descriptor_flags==UOS_CLOEXEC;
        readers+=entry->flags==UOS_READ;
    }
    const struct uos_fd_entry *unused=&table.entries[table.count];
    return marked==2 && readers==1 && !unused->token && !unused->bytes &&
           !unused->offset && !unused->flags && !unused->descriptor_flags;
}

static bool check_after_exec(void)
{
    struct uos_fd_table table;
    if (uos_fd_list(&table) || table.count!=1) { return false; }
    const struct uos_fd_entry *entry=&table.entries[0];
    char bytes[2];
    if (entry->flags!=(UOS_READ|UOS_WRITE) || entry->descriptor_flags ||
        entry->bytes!=4 || entry->offset!=2 ||
        uos_read(entry->token,bytes,sizeof(bytes))!=sizeof(bytes) ||
        bytes[0]!='c' || bytes[1]!='!' || uos_close(entry->token) ||
        uos_fd_dup(entry->token,0,0)!=UOS_EBADF || uos_fd_list(&table) || table.count) { return false; }
    return !uos_remove(sample,0) && !uos_remove(scratch,1);
}

int app_main(void)
{
    if (uos_argc()==1 && !uos_mkdir(scratch)) {
        int64_t initial=uos_open(sample,UOS_READ|UOS_WRITE|UOS_CREATE|UOS_EXCL|UOS_CLOEXEC);
        if (initial>0 && exercise((uint64_t)initial) && check_before_exec()) {
            struct app_args args;
            exec_args(&args,"/boot/NONE.ELF");
            if (uos_exec(&args)<0 && check_before_exec()) {
                exec_args(&args,"/boot/FDDUP.ELF");
                (void)uos_exec(&args);
            }
        }
    } else if (uos_argc()==2) {
        char mode[5];
        if (uos_arg(1,mode,sizeof(mode))==4 &&
            mode[0]=='p' && mode[1]=='o' && mode[2]=='s' && mode[3]=='t' && check_after_exec()) {
            uos_puts("PASS: descriptor duplication, rights, append control, close-on-exec and inventory.\n");
            return 0;
        }
    }
    uos_puts("FAIL: descriptor lifecycle checks.\n");
    return 1;
}
