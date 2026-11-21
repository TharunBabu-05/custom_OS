#include "user.h"
#include "allocator.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static int zeroed(const void *p,size_t n) { const volatile unsigned char *b=p;for (size_t i=0;i<n;++i) { if (b[i]) { return 0; } }return 1; }
static void fill(void *p,size_t n,unsigned char byte) { volatile unsigned char *b=p;for (size_t i=0;i<n;++i) { b[i]=byte; } }
static int filled(const void *p,size_t n,unsigned char byte) { const volatile unsigned char *b=p;for (size_t i=0;i<n;++i) { if (b[i]!=byte) { return 0; } }return 1; }
static void word(struct app_args *a,const char *s) { a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static void make(struct app_args *a,const char *mode) { *a=(struct app_args){0};word(a,"/boot/HEAPCHK.ELF");word(a,mode); }
static void number(char *out,uint64_t n)
{ char d[24];unsigned i=0;do { d[i++]=(char)('0'+n%10);n/=10; }while (n);for (unsigned j=0;j<i;++j) { out[j]=d[i-1-j]; }out[i]=0; }
static uint64_t value(const char *s) { uint64_t n=0;while (*s>='0' && *s<='9') { n=n*10+(unsigned)(*s++-'0'); }return n; }
static int basic(void)
{
    struct uos_allocator a={0},b={0};struct uos_memory_info info;
    if (uos_memory_info(&info) || info.live_pages || !uos_allocator_init(&a) || !uos_allocator_init(&b)) { return 1; }
    const unsigned sizes[]={1,17,33,512,2048,5000};unsigned char *objects[6];
    for (unsigned i=0;i<6;++i) {
        objects[i]=uos_malloc(&a,sizes[i]);if (!objects[i] || ((uintptr_t)objects[i]&15) || !zeroed(objects[i],sizes[i])) { return 2; }fill(objects[i],sizes[i],0x51);
    }
    unsigned char *array=uos_calloc(&a,16,16);if (!array || !zeroed(array,256) || uos_malloc(&a,0) || uos_malloc(&a,SIZE_MAX) || uos_calloc(&a,SIZE_MAX,2)) { return 3; }
    if (uos_free(&a,objects[1]+1) || uos_free(&b,objects[1]) || uos_free(&a,&a) || !uos_free(&a,NULL) || uos_allocator_init(&a)) { return 4; }
    if (uos_realloc(&a,objects[1],31)!=objects[1] || !filled(objects[1],17,0x51) || !zeroed(objects[1]+17,14) ||
        uos_realloc(&a,objects[1],4)!=objects[1] || uos_realloc(&a,objects[1],30)!=objects[1] || !filled(objects[1],4,0x51) || !zeroed(objects[1]+4,26)) { return 5; }
    unsigned char *old=objects[5],*grown=uos_realloc(&a,old,9000);
    if (!grown || !filled(grown,5000,0x51) || !zeroed(grown+5000,4000) || uos_free(&a,old)) { return 6; }objects[5]=grown;fill(grown,9000,0x6e);
    if (uos_realloc(&a,grown,65536) || !filled(grown,9000,0x6e)) { return 7; }
    for (unsigned i=0;i<6;++i) { if (!uos_free(&a,objects[i])) { return 8; } }
    if (!uos_free(&a,array) || !uos_allocator_destroy(&a) || !uos_allocator_destroy(&b) || uos_memory_info(&info) || info.live_pages) { return 9; }
    if (!uos_allocator_init(&a)) { return 10; }void *small[64];unsigned char pattern=(unsigned char)(uos_getpid()%250+1);
    for (unsigned i=0;i<64;++i) { small[i]=uos_malloc(&a,8);if (!small[i] || !zeroed(small[i],8)) { return 11; }fill(small[i],8,pattern); }
    if (uos_memory_info(&info) || info.live_pages!=1 || uos_malloc(&a,8) || uos_realloc(&a,small[0],64)) { return 12; }
    struct uos_allocator_stats stats=uos_allocator_stats(&a);
    if (stats.live_blocks!=64 || stats.requested_bytes!=512 || stats.pages!=1 || stats.poisoned) { return 13; }
    for (unsigned round=0;round<3;++round) {
        if (uos_yield() || uos_sleep_ms(10)) { return 14; }
        for (unsigned i=0;i<64;++i) { if (!filled(small[i],8,pattern)) { return 15; } }
    }
    for (unsigned i=0;i<64;++i) { if (!uos_free(&a,small[i])) { return 16; } }
    if (uos_free(&a,small[0]) || uos_memory_info(&info) || info.live_pages) { return 17; }
    void *all=uos_malloc(&a,65536);if (!all || !zeroed(all,65536) || uos_malloc(&a,1) || !uos_free(&a,all)) { return 18; }
    if (!uos_malloc(&a,123) || !uos_malloc(&a,7000) || !uos_allocator_destroy(&a) || uos_memory_info(&info) || info.live_pages) { return 19; }
    uos_puts("PASS: user malloc/calloc/realloc/free alignment, shared small pages, zeroed growth, failure retention, invalid pointers, record/quota limits and complete cleanup.\n");return 0;
}
static int resize_check(void)
{
    int64_t address=uos_alloc_pages(10);
    if (address<0) { return 32; }
    unsigned char *base=(void *)(uintptr_t)address;
    fill(base,40000,0x51);
    int64_t neighbor=uos_alloc_pages(1);
    if (neighbor<0 || uos_resize_pages(base,15)!=UOS_ENOMEM ||
        uos_resize_pages(base+4096,2)!=UOS_ERROR || uos_resize_pages(base,0)!=UOS_ERROR ||
        uos_free_pages((void *)(uintptr_t)neighbor) || !filled(base,40000,0x51)) { return 33; }
    if (uos_resize_pages(base,15) || !filled(base,40000,0x51) || !zeroed(base+40960,20480)) { return 34; }
    if (uos_resize_pages(base,2) || uos_write(base+8192,1)!=UOS_EFAULT || !filled(base,8192,0x51)) { return 35; }
    neighbor=uos_alloc_pages(14);
    if (neighbor!=address+8192 || !zeroed((void *)(uintptr_t)neighbor,14*4096) ||
        uos_free_pages((void *)(uintptr_t)neighbor) || uos_free_pages(base)) { return 36; }

    struct uos_allocator heap={0};
    struct uos_memory_info info;
    if (!uos_allocator_init(&heap)) { return 38; }
    base=uos_malloc(&heap,40000);
    if (!base) { return 39; }
    fill(base,40000,0x62);
    if (uos_realloc(&heap,base,60000)!=base || !filled(base,40000,0x62) ||
        !zeroed(base+40000,20000) || uos_memory_info(&info) || info.live_pages!=15) { return 40; }
    if (uos_realloc(&heap,base,5000)!=base || !filled(base,5000,0x62) ||
        uos_memory_info(&info) || info.live_pages!=2 || uos_write(base+8192,1)!=UOS_EFAULT) { return 41; }
    if (uos_realloc(&heap,base,7000)!=base || !zeroed(base+5000,2000)) { return 42; }
    if (!uos_free(&heap,base) || !uos_allocator_destroy(&heap) ||
        uos_memory_info(&info) || info.live_pages) { return 43; }
    uos_puts("PASS: in-place page resize and realloc, preserved contents, zeroed growth and reclaimed tail pages.\n");
    return 0;
}
int app_main(void)
{
    if (uos_argc()==1) { return basic(); }char mode[24],digits[24];if (uos_arg(1,mode,sizeof(mode))<0) { return 20; }
    if (same(mode,"parent")) {
        const char *modes[]={"leak","exec","execbad","uaf","nx","timeout"};const int codes[]={23,37,0,125,125,124};
        for (unsigned round=0;round<2;++round) { for (unsigned i=0;i<6;++i) {
            struct app_args args;make(&args,modes[i]);int64_t pid=uos_spawn(&args,i==5 ? 1 : 3);
            if (pid<0 || uos_wait((uint64_t)pid)!=codes[i]) { return 21; }
        } }
        uos_puts("PASS: byte-allocator leaks reclaimed on exit/exec/fault/timeout; failed exec preserves context/data and child slots remain reusable.\n");return 0;
    }
    if (same(mode,"resize")) { return resize_check(); }
    struct uos_allocator heap={0};if (!uos_allocator_init(&heap)) { return 22; }
    if (same(mode,"after")) {
        struct uos_memory_info info;
        if (uos_argc()!=3 || uos_arg(2,digits,sizeof(digits))<0 || value(digits)!=(uint64_t)uos_getpid() || uos_memory_info(&info) || info.live_pages) { return 24; }
        void *p=uos_calloc(&heap,60000,1);if (!p || !zeroed(p,60000) || !uos_free(&heap,p) || !uos_allocator_destroy(&heap)) { return 25; }return 37;
    }
    size_t bytes=same(mode,"timeout") ? 65536 : same(mode,"uaf") || same(mode,"nx") ? 32 : 20000;
    unsigned char *p=uos_malloc(&heap,bytes);if (!p) { return 26; }fill(p,bytes,0x6a);
    if (same(mode,"leak")) { return 23; }
    if (same(mode,"exec")) { struct app_args args;make(&args,"after");number(digits,(uint64_t)uos_getpid());word(&args,digits);(void)uos_exec(&args);return 27; }
    if (same(mode,"execbad")) {
        struct app_args args={0};word(&args,"/boot/heap-missing.elf");
        if (uos_exec(&args)!=UOS_ERROR || !filled(p,bytes,0x6a) || uos_allocator_stats(&heap).live_blocks!=1) { return 28; }return 0;
    }
    if (same(mode,"uaf")) { if (!uos_free(&heap,p)) { return 29; }return *(volatile unsigned char *)p; }
    if (same(mode,"nx")) { *(volatile uint32_t *)p=0xd65f03c0;((void (*)(void))(uintptr_t)p)();return 30; }
    if (same(mode,"cancel")) { uos_puts("HEAPCHK ready for cancellation.\n"); }
    if (same(mode,"timeout") || same(mode,"cancel")) { for (;;) { __asm__ volatile("" ::: "memory"); } }
    return 31;
}
