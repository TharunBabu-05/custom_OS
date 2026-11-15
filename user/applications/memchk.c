#include "user.h"
#include "user_mmu.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static int zeroed(const volatile unsigned char *p,size_t n) { for (size_t i=0;i<n;++i) { if (p[i]) { return 0; } }return 1; }
static void fill(volatile unsigned char *p,size_t n,unsigned char byte) { for (size_t i=0;i<n;++i) { p[i]=byte; } }
static int filled(const volatile unsigned char *p,size_t n,unsigned char byte) { for (size_t i=0;i<n;++i) { if (p[i]!=byte) { return 0; } }return 1; }
static void word(struct app_args *a,const char *s) { a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static void make(struct app_args *a,const char *mode) { *a=(struct app_args){0};word(a,"/boot/MEMCHK.ELF");word(a,mode); }
static void number(char *out,uint64_t n)
{ char digits[24];unsigned used=0;do { digits[used++]=(char)('0'+n%10);n/=10; }while (n);for (unsigned i=0;i<used;++i) { out[i]=digits[used-1-i]; }out[used]=0; }
static uint64_t value(const char *s) { uint64_t n=0;while (*s>='0' && *s<='9') { n=n*10+(unsigned)(*s++-'0'); }return n; }
static int basics(void)
{
    struct uos_memory_info info;unsigned char bytes[8];
    if (uos_memory_info(&info) || info.live_pages || info.page_bytes!=4096 || info.limit_pages!=16 ||
        uos_memory_info(0)!=UOS_EFAULT || uos_memory_info((struct uos_memory_info *)(uintptr_t)"readonly")!=UOS_EFAULT) { return 1; }
    if (uos_alloc_pages(0)!=UOS_ERROR || uos_alloc_pages(17)!=UOS_ERROR || uos_call(UOS_SYS_ALLOC_PAGES,UINT64_MAX,0,0,0,0,0)!=UOS_ERROR ||
        uos_free_pages(0)!=UOS_ERROR || uos_free_pages((void *)(uintptr_t)USER_VA_BASE)!=UOS_ERROR || uos_free_pages((void *)(uintptr_t)USER_STACK_BASE)!=UOS_ERROR) { return 2; }
    int64_t address=uos_alloc_pages(3);if (address!=(int64_t)USER_HEAP_BASE) { return 3; }volatile unsigned char *a=(void *)(uintptr_t)address;
    if (!zeroed(a,3*4096)) { return 4; }fill(a,3*4096,0x39);
    int64_t second=uos_alloc_pages(13);if (second!=address+3*4096 || !zeroed((void *)(uintptr_t)second,13*4096) ||
        !filled(a,3*4096,0x39) || uos_alloc_pages(1)!=UOS_ENOMEM || uos_memory_info(&info) || info.live_pages!=16 || info.peak_pages!=16) { return 5; }
    int64_t h=uos_open("/etc/hostname",UOS_READ);
    if (h<0 || uos_read((uint64_t)h,(void *)(uintptr_t)(address+4093),6)!=6 || a[4093]!='o' || a[4098]!='\n') { return 6; }
    if (uos_memory_info((struct uos_memory_info *)(uintptr_t)(USER_HEAP_BASE+16*4096-16))!=UOS_EFAULT ||
        uos_free_pages((void *)(uintptr_t)(address+1))!=UOS_ERROR || uos_free_pages((void *)(uintptr_t)(second+4096))!=UOS_ERROR ||
        uos_free_pages((void *)(uintptr_t)address) || uos_free_pages((void *)(uintptr_t)address)!=UOS_ERROR) { return 7; }
    if (uos_seek((uint64_t)h,0,UOS_SEEK_SET) || uos_read((uint64_t)h,(void *)(uintptr_t)address,1)!=UOS_EFAULT ||
        uos_write((const void *)(uintptr_t)address,1)!=UOS_EFAULT || uos_memory_info((void *)(uintptr_t)address)!=UOS_EFAULT ||
        uos_read((uint64_t)h,bytes,3)!=3 || bytes[0]!='o' || bytes[1]!='u' || bytes[2]!='r' || uos_close((uint64_t)h)) { return 8; }
    int64_t reused=uos_alloc_pages(3);if (reused!=address || !zeroed((void *)(uintptr_t)reused,3*4096) ||
        uos_free_pages((void *)(uintptr_t)reused) || uos_free_pages((void *)(uintptr_t)second)) { return 9; }
    uint64_t pages[16];unsigned char pattern=(unsigned char)(uos_getpid()%250+1);
    for (unsigned i=0;i<16;++i) {
        int64_t r=uos_alloc_pages(1);if (r<0 || !zeroed((void *)(uintptr_t)r,4096)) { return 10; }pages[i]=(uint64_t)r;fill((void *)(uintptr_t)r,4096,pattern);
    }
    for (unsigned round=0;round<3;++round) {
        if (uos_yield() || uos_sleep_ms(10)) { return 11; }
        for (unsigned i=0;i<16;++i) { if (!filled((void *)(uintptr_t)pages[i],4096,pattern)) { return 12; } }
    }
    for (unsigned i=0;i<16;i+=2) { if (uos_free_pages((void *)(uintptr_t)pages[i])) { return 13; } }
    if (uos_alloc_pages(2)!=UOS_ENOMEM) { return 14; }
    for (unsigned i=1;i<16;i+=2) { if (uos_free_pages((void *)(uintptr_t)pages[i])) { return 15; } }
    int64_t all=uos_alloc_pages(16);
    if (all!=(int64_t)USER_HEAP_BASE || !zeroed((void *)(uintptr_t)all,16*4096) || uos_free_pages((void *)(uintptr_t)all) ||
        uos_memory_info(&info) || info.live_pages || info.peak_pages!=16 || info.allocations!=20 || info.free_calls!=20) { return 16; }
    uos_puts("PASS: private dynamic pages zeroed, quota/fragmentation/reuse, cross-page syscall buffers, freed-pointer rejection, concurrent isolation and complete release.\n");return 0;
}
int app_main(void)
{
    char mode[24],text[24];int64_t argc=uos_argc();if (argc==1) { return basics(); }
    if (uos_arg(1,mode,sizeof(mode))<0) { return 17; }
    if (same(mode,"parent")) {
        const char *modes[]={"leak","exec","execbad","uaf","nx","guard","upper","timeout"};const int codes[]={23,37,0,125,125,125,125,124};
        for (unsigned round=0;round<3;++round) { for (unsigned i=0;i<8;++i) {
            struct app_args a;make(&a,modes[i]);int64_t pid=uos_spawn(&a,i==7 ? 1 : 3);
            if (pid<0 || uos_wait((uint64_t)pid)!=codes[i]) { return 18; }
        } }
        struct uos_memory_info info;if (uos_memory_info(&info) || info.live_pages) { return 19; }
        uos_puts("PASS: leaked pages reclaimed on child exit/exec/fault/timeout; exec identity/cursor retained, failed exec preserves pages and repeated slots stay clean.\n");return 0;
    }
    if (same(mode,"after")) {
        struct uos_memory_info info;if (argc!=4 || uos_arg(2,text,sizeof(text))<0 || value(text)!=(uint64_t)uos_getpid() ||
            uos_memory_info(&info) || info.live_pages || info.peak_pages!=16 || uos_write((const void *)(uintptr_t)USER_HEAP_BASE,1)!=UOS_EFAULT) { return 20; }
        if (uos_arg(3,text,sizeof(text))<0) { return 21; }uint64_t h=value(text);unsigned char bytes[4];
        if (uos_read(h,bytes,4)!=4 || bytes[0]!='r' || bytes[3]!='\n' || uos_close(h)) { return 22; }
        int64_t r=uos_alloc_pages(16);if (r<0 || !zeroed((void *)(uintptr_t)r,16*4096) || uos_free_pages((void *)(uintptr_t)r)) { return 24; }return 37;
    }
    int64_t address=uos_alloc_pages(16);if (address<0 || !zeroed((void *)(uintptr_t)address,16*4096)) { return 25; }
    volatile unsigned char *p=(void *)(uintptr_t)address;fill(p,16*4096,0x5a);
    if (same(mode,"leak")) { return 23; }
    if (same(mode,"execbad")) {
        struct app_args a={0};word(&a,"/boot/not-memory.elf");struct uos_memory_info info;
        return uos_exec(&a)==UOS_ERROR && filled(p,16*4096,0x5a) && !uos_memory_info(&info) && info.live_pages==16 ? 0 : 26;
    }
    if (same(mode,"exec")) {
        int64_t h=uos_open("/etc/hostname",UOS_READ);char bytes[2];if (h<0 || uos_read((uint64_t)h,bytes,2)!=2) { return 27; }
        struct app_args a;make(&a,"after");number(text,(uint64_t)uos_getpid());word(&a,text);number(text,(uint64_t)h);word(&a,text);(void)uos_exec(&a);return 28;
    }
    if (same(mode,"uaf")) { if (uos_free_pages((void *)(uintptr_t)address)) { return 29; }return p[0]; }
    if (same(mode,"nx")) { *(volatile uint32_t *)(uintptr_t)address=0xd65f03c0;((void (*)(void))(uintptr_t)address)();return 30; }
    if (same(mode,"guard")) { *(volatile unsigned char *)(uintptr_t)(USER_HEAP_BASE-1)=1;return 31; }
    if (same(mode,"upper")) { p[16*4096]=1;return 32; }
    if (same(mode,"cancel")) { uos_puts("MEMCHK ready for cancellation.\n"); }
    if (same(mode,"timeout") || same(mode,"cancel")) { for (;;) { __asm__ volatile("" ::: "memory"); } }
    return 33;
}
