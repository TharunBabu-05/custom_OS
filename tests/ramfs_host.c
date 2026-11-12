/* Native tests use real host buffers as mock physical pages. */
#include "host_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "ramfs.h"
#include "memory.h"
#include "heap.h"
#include "console.h"
static unsigned char *storage;
static bool occupied[32];
static unsigned live;
static int budget=-1;
struct page_stats pages_stats(void) { return (struct page_stats){.ready=true,.allocated_pages=live,.free_pages=32-live}; }
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
    assert(ramfs_init("host-test")==FS_OK && live==2);
    struct ramfs_stats s=ramfs_get_stats(); assert(s.ready && s.files==3 && s.directories==12 && s.pages==2 && s.buffers==3 && s.capacity_bytes==544 && heap_get_stats().objects==3);
    content(0,"/etc/hostname","ouros\n"); content(0,"/etc/board","host-test\n");
    unsigned count=0; assert(ramfs_list(0,"/",count_entry,&count)==FS_OK && count==10);
    int cwd=-1; assert(ramfs_chdir(0,"/home//user/.././user",&cwd)==FS_OK);
    char path[RAMFS_PATH_MAX]; assert(ramfs_pwd(cwd,path,sizeof(path))==FS_OK && !strcmp(path,"/home/user"));
    assert(ramfs_chdir(cwd,"../../../../",&cwd)==FS_OK && cwd==0);
    assert(ramfs_pwd(0,path,1)==FS_PATH_TOO_LONG);
    assert(ramfs_mkdir(0,"/tmp/work/")==FS_OK);
    assert(ramfs_chdir(0,"/tmp/work",&cwd)==FS_OK);
    assert(ramfs_touch(cwd,"empty")==FS_OK && live==2); content(cwd,"empty","");
    assert(ramfs_write(cwd,"note","hello world",11)==FS_OK && live==2); content(cwd,"./note","hello world");
    assert(ramfs_touch(cwd,"note")==FS_OK); content(cwd,"note","hello world");
    assert(ramfs_write(cwd,"note","abcdef",6)==FS_OK && live==2);
    const char *data; size_t length; assert(ramfs_read(cwd,"note",&data,&length)==FS_OK);
    assert(ramfs_write(cwd,"note",data+2,4)==FS_OK); content(cwd,"note","cdef");
    assert(ramfs_write(cwd,"note",NULL,0)==FS_OK && live==2); content(cwd,"note","");
    char page[4097]; for (unsigned i=0;i<sizeof(page);++i) { page[i]=(char)(i%251); }
    assert(ramfs_write(cwd,"note",page,4096)==FS_OK && live==3);
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
    assert(ramfs_remove(cwd,"note",false)==FS_OK && live==2);
    assert(ramfs_remove(cwd,"empty",false)==FS_OK);
    assert(ramfs_remove(0,"/tmp/work",true)==FS_OK);
    s=ramfs_get_stats(); assert(s.files==3 && s.directories==12 && s.pages==2 && heap_check());
    puts("PASS: seeded root, paths/cwd, binary files, overlapping overwrite/truncate/removal, heap ownership and small-file page sharing");
}
static void failures(void)
{
    assert(ramfs_init("host-test")==FS_OK && live==2);
    assert(ramfs_touch(0,"/tmp/empty")==FS_OK);
    char text[65];memset(text,'a',sizeof(text));assert(ramfs_write(0,"/tmp/old","preserved",9)==FS_OK);
    budget=0; struct ramfs_stats before=ramfs_get_stats();
    assert(ramfs_write(0,"/tmp/new",text,33)==FS_NO_MEMORY);
    assert(ramfs_write(0,"/tmp/empty",text,33)==FS_NO_MEMORY); content(0,"/tmp/empty","");
    assert(ramfs_write(0,"/tmp/old",text,33)==FS_NO_MEMORY);content(0,"/tmp/old","preserved");
    struct ramfs_stats after=ramfs_get_stats();
    assert(after.files==before.files && after.pages==before.pages && after.buffers==before.buffers && after.bytes==before.bytes && after.capacity_bytes==before.capacity_bytes && live==2 && heap_check());
    budget=1; assert(ramfs_init("host-test")==FS_NO_MEMORY && !ramfs_get_stats().ready && live==0);
    assert(ramfs_mkdir(0,"/tmp")==FS_NOT_READY);
    budget=-1; assert(ramfs_init("host-test")==FS_OK && live==2);
    unsigned added=0;
    for (;;) {
        char path[32]; snprintf(path,sizeof(path),"/tmp/f%u",added);
        enum fs_result result=ramfs_touch(0,path);
        if (result==FS_NO_NODES) { break; }
        assert(result==FS_OK); ++added; assert(added<=RAMFS_NODES);
    }
    assert(added==RAMFS_NODES-15 && live==2);
    assert(ramfs_write(0,"/tmp/full","data",4)==FS_NO_NODES && live==2);
    assert(ramfs_remove(0,"/tmp/f0",false)==FS_OK);
    assert(ramfs_write(0,"/tmp/reused","data",4)==FS_OK && live==2);
    assert(ramfs_init("host-test")==FS_OK && live==2);
    char long_board[65]; memset(long_board,'a',64); long_board[64]='\0';
    assert(ramfs_init(long_board)==FS_BAD_ARGUMENT && live==0 && !ramfs_get_stats().ready);
    assert(ramfs_init("host-test")==FS_OK && live==2);
    puts("PASS: failed new/growing writes preserve nodes/data/heap counters, initialization rollback, node exhaustion/reuse and shared-page reclamation");
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
static void shared_files(void)
{
    assert(ramfs_init("host-test")==FS_OK && live==2);char path[32];unsigned char expected[32][16];
    for (unsigned i=0;i<32;++i) {
        snprintf(path,sizeof(path),"/tmp/shared%u",i);for (unsigned j=0;j<16;++j) { expected[i][j]=(unsigned char)(i*3+j); }
        assert(ramfs_write(0,path,(const char *)expected[i],16)==FS_OK && live==2 && heap_check());
    }
    struct ramfs_stats s=ramfs_get_stats();assert(s.buffers==35 && s.pages==2 && s.capacity_bytes==544+32*16);
    for (unsigned i=0;i<32;i+=2) { snprintf(path,sizeof(path),"/tmp/shared%u",i);assert(ramfs_remove(0,path,false)==FS_OK && live==2); }
    for (unsigned i=1;i<32;i+=2) {
        const char *p;size_t bytes;snprintf(path,sizeof(path),"/tmp/shared%u",i);assert(ramfs_read(0,path,&p,&bytes)==FS_OK && bytes==16 && !memcmp(p,expected[i],16));
        assert(ramfs_write(0,path,p+5,11)==FS_OK);assert(ramfs_read(0,path,&p,&bytes)==FS_OK && bytes==11 && !memcmp(p,expected[i]+5,11));
        for (unsigned j=11;j<16;++j) { assert(!p[j]); }assert(ramfs_remove(0,path,false)==FS_OK);
    }
    assert(ramfs_get_stats().buffers==3 && heap_get_stats().objects==3 && live==2 && heap_check());
    assert(ramfs_init(NULL)==FS_BAD_ARGUMENT && live==0 && !heap_get_stats().objects);
    puts("PASS: 32 binary files share existing class pages, distinct buffers/data, alternating deletes and aliased truncations preserve every neighbor and erase tails");
}
int main(void)
{
    storage=VirtualAlloc(NULL,32*PAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); assert(storage);
    assert(heap_init());sizes(); basics(); failures(); paths_and_guards();shared_files();assert(live==0 && heap_check() && !heap_get_stats().objects);
    VirtualFree(storage,0,MEM_RELEASE); return 0;
}
