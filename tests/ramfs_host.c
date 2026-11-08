/* Native tests use real host buffers as mock physical pages. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "ramfs.h"
#include "memory.h"
#include "console.h"
static unsigned char *storage;
static bool occupied[32];
static unsigned live;
static int budget=-1;
bool page_alloc(uint64_t *physical)
{
    if (!physical || budget==0) { return false; }
    for (unsigned i=0;i<32;++i) {
        if (!occupied[i]) {
            occupied[i]=true; ++live; if (budget>0) { --budget; }
            *physical=(uintptr_t)(storage+i*PAGE_SIZE); return true;
        }
    }
    return false;
}
bool page_free(uint64_t physical)
{
    uint64_t base=(uintptr_t)storage;
    if (physical<base || physical-base>=32*PAGE_SIZE || ((physical-base)&(PAGE_SIZE-1))) { return false; }
    unsigned i=(unsigned)((physical-base)/PAGE_SIZE);
    if (!occupied[i]) { return false; } occupied[i]=false; --live; return true;
}
static char console_output[256]; static size_t output_length;
void uart_putc(char c) { assert(output_length+1<sizeof(console_output)); console_output[output_length++]=c; console_output[output_length]='\0'; }
static void sizes(void)
{
    const uint64_t bytes[]={0,4096,8585740288ULL,7516192768ULL,UINT64_MAX};
    const char *expected[]={"0.000 MB (0.000 MiB)","0.004 MB (0.003 MiB)",
        "8585.740 MB (8188.000 MiB)","7516.192 MB (7168.000 MiB)",
        "18446744073709.551 MB (17592186044415.999 MiB)"};
    for (unsigned i=0;i<sizeof(bytes)/sizeof(bytes[0]);++i) {
        output_length=0; console_output[0]='\0'; console_memory_size(bytes[i]); assert(!strcmp(console_output,expected[i]));
    }
    puts("PASS: decimal MB/binary MiB formatting, fractional pages and 64-bit limits");
}
static void content(int cwd,const char *path,const char *expected)
{
    const char *data; size_t length;
    assert(ramfs_read(cwd,path,&data,&length)==FS_OK);
    assert(length==strlen(expected) && !memcmp(data,expected,length));
}
static void count_entry(const char *name,bool directory,size_t bytes,void *context)
{ assert(name && *name); (void)directory; (void)bytes; ++*(unsigned *)context; }
static void basics(void)
{
    assert(ramfs_init("host-test")==FS_OK && live==3);
    struct ramfs_stats s=ramfs_get_stats(); assert(s.ready && s.files==3 && s.directories==5 && s.pages==3);
    content(0,"/etc/hostname","ouros\n"); content(0,"/etc/board","host-test\n");
    unsigned count=0; assert(ramfs_list(0,"/",count_entry,&count)==FS_OK && count==4);
    int cwd=-1; assert(ramfs_chdir(0,"/home//user/.././user",&cwd)==FS_OK);
    char path[RAMFS_PATH_MAX]; assert(ramfs_pwd(cwd,path,sizeof(path))==FS_OK && !strcmp(path,"/home/user"));
    assert(ramfs_chdir(cwd,"../../../../",&cwd)==FS_OK && cwd==0);
    assert(ramfs_pwd(0,path,1)==FS_PATH_TOO_LONG);
    assert(ramfs_mkdir(0,"/tmp/work/")==FS_OK);
    assert(ramfs_chdir(0,"/tmp/work",&cwd)==FS_OK);
    assert(ramfs_touch(cwd,"empty")==FS_OK && live==3); content(cwd,"empty","");
    assert(ramfs_write(cwd,"note","hello world",11)==FS_OK && live==4); content(cwd,"./note","hello world");
    assert(ramfs_touch(cwd,"note")==FS_OK); content(cwd,"note","hello world");
    assert(ramfs_write(cwd,"note","abcdef",6)==FS_OK && live==4);
    const char *data; size_t length; assert(ramfs_read(cwd,"note",&data,&length)==FS_OK);
    assert(ramfs_write(cwd,"note",data+2,4)==FS_OK); content(cwd,"note","cdef");
    assert(ramfs_write(cwd,"note",NULL,0)==FS_OK && live==3); content(cwd,"note","");
    char page[4097]; for (unsigned i=0;i<sizeof(page);++i) { page[i]=(char)(i%251); }
    assert(ramfs_write(cwd,"note",page,4096)==FS_OK && live==4);
    assert(ramfs_write(cwd,"note",page,4097)==FS_FILE_TOO_LARGE);
    assert(ramfs_read(cwd,"note",&data,&length)==FS_OK && length==4096 && !memcmp(data,page,length));
    assert(ramfs_remove(cwd,".",true)==FS_BUSY);
    assert(ramfs_remove(0,"/tmp/work",true)==FS_NOT_EMPTY);
    assert(ramfs_remove(cwd,"note",true)==FS_NOT_DIR);
    assert(ramfs_remove(0,"/tmp",false)==FS_IS_DIR);
    assert(ramfs_remove(0,"/",true)==FS_BUSY);
    assert(ramfs_read(0,"/etc",&data,&length)==FS_IS_DIR);
    int unchanged=99; assert(ramfs_chdir(0,"/etc/hostname",&unchanged)==FS_NOT_DIR && unchanged==99);
    assert(ramfs_lookup(0,"/etc/hostname/..",&unchanged)==FS_NOT_DIR);
    assert(ramfs_lookup(0,"/missing/../etc",&unchanged)==FS_NOT_FOUND);
    assert(ramfs_write(0,"/etc",page,1)==FS_IS_DIR);
    assert(ramfs_mkdir(0,"/tmp/work")==FS_EXISTS);
    assert(ramfs_mkdir(0,"/tmp/.")==FS_INVALID_PATH);
    assert(ramfs_touch(0,"/tmp/name with spaces")==FS_INVALID_PATH);
    assert(ramfs_write(0,"/missing/file",page,1)==FS_NOT_FOUND);
    assert(ramfs_remove(cwd,"note",false)==FS_OK && live==3);
    assert(ramfs_remove(cwd,"empty",false)==FS_OK);
    assert(ramfs_remove(0,"/tmp/work",true)==FS_OK);
    s=ramfs_get_stats(); assert(s.files==3 && s.directories==5 && s.pages==3);
    puts("PASS: seeded root, paths/cwd, binary files, overwrite/truncate, removal and page ownership");
}
static void failures(void)
{
    assert(ramfs_init("host-test")==FS_OK && live==3);
    assert(ramfs_touch(0,"/tmp/empty")==FS_OK);
    budget=0; struct ramfs_stats before=ramfs_get_stats();
    assert(ramfs_write(0,"/tmp/new","x",1)==FS_NO_MEMORY);
    assert(ramfs_write(0,"/tmp/empty","x",1)==FS_NO_MEMORY); content(0,"/tmp/empty","");
    struct ramfs_stats after=ramfs_get_stats();
    assert(after.files==before.files && after.pages==before.pages && live==3);
    budget=1; assert(ramfs_init("host-test")==FS_NO_MEMORY && !ramfs_get_stats().ready && live==0);
    assert(ramfs_mkdir(0,"/tmp")==FS_NOT_READY);
    budget=-1; assert(ramfs_init("host-test")==FS_OK && live==3);
    unsigned added=0;
    for (;;) {
        char path[32]; snprintf(path,sizeof(path),"/tmp/f%u",added);
        enum fs_result result=ramfs_touch(0,path);
        if (result==FS_NO_NODES) { break; }
        assert(result==FS_OK); ++added; assert(added<=RAMFS_NODES);
    }
    assert(added==RAMFS_NODES-8 && live==3);
    assert(ramfs_write(0,"/tmp/full","data",4)==FS_NO_NODES && live==3);
    assert(ramfs_remove(0,"/tmp/f0",false)==FS_OK);
    assert(ramfs_write(0,"/tmp/reused","data",4)==FS_OK && live==4);
    assert(ramfs_init("host-test")==FS_OK && live==3);
    char long_board[65]; memset(long_board,'a',64); long_board[64]='\0';
    assert(ramfs_init(long_board)==FS_BAD_ARGUMENT && live==0 && !ramfs_get_stats().ready);
    assert(ramfs_init("host-test")==FS_OK && live==3);
    puts("PASS: atomic out-of-memory handling, initialization rollback, node exhaustion/reuse and page reclamation");
}
static void paths_and_guards(void)
{
    char component[33]; memset(component,'a',32); component[32]='\0'; char path[300];
    snprintf(path,sizeof(path),"/tmp/%s",component); assert(ramfs_touch(0,path)==FS_NAME_TOO_LONG);
    char deep[RAMFS_PATH_MAX+2]=""; component[31]='\0';
    for (unsigned i=0;i<7;++i) { strcat(deep,"/"); strcat(deep,component); assert(ramfs_mkdir(0,deep)==FS_OK); }
    assert(strlen(deep)==224); strcat(deep,"/"); strcat(deep,component);
    assert(strlen(deep)==256); assert(ramfs_mkdir(0,deep)==FS_PATH_TOO_LONG);
    deep[255]='\0'; assert(ramfs_mkdir(0,deep)==FS_OK);
    int deepest; assert(ramfs_chdir(0,deep,&deepest)==FS_OK);
    char canonical[RAMFS_PATH_MAX]; assert(ramfs_pwd(deepest,canonical,sizeof(canonical))==FS_OK);
    assert(strlen(canonical)==255 && !strcmp(canonical,deep));
    assert(ramfs_mkdir(deepest,"x")==FS_PATH_TOO_LONG);
    SYSTEM_INFO info; GetSystemInfo(&info);
    unsigned char *allocation=VirtualAlloc(NULL,2*info.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(allocation); DWORD old; assert(VirtualProtect(allocation+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old));
    char *guarded=(char *)allocation+info.dwPageSize-RAMFS_PATH_MAX; memset(guarded,'a',RAMFS_PATH_MAX);
    int node; assert(ramfs_lookup(0,guarded,&node)==FS_PATH_TOO_LONG);
    uint32_t random=0x1234abcd;
    for (unsigned i=0;i<5000;++i) {
        for (unsigned j=0;j<RAMFS_PATH_MAX-1;++j) {
            random=random*1664525u+1013904223u; guarded[j]="abc./_- "[random%8];
        }
        guarded[RAMFS_PATH_MAX-1]='\0'; (void)ramfs_lookup(0,guarded,&node);
    }
    VirtualFree(allocation,0,MEM_RELEASE);
    assert(ramfs_init(NULL)==FS_BAD_ARGUMENT && live==0);
    puts("PASS: canonical path bounds, name limits, guard-page input and 5000 path mutations");
}
int main(void)
{
    storage=VirtualAlloc(NULL,32*PAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); assert(storage);
    sizes(); basics(); failures(); paths_and_guards(); assert(live==0);
    VirtualFree(storage,0,MEM_RELEASE); return 0;
}
