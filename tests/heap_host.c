/* The actual heap implementation, with guarded mock PMM pages. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "host_assert.h"
#include "heap.h"
#include "memory.h"
enum { HOST_PAGES=HEAP_MAX_PAGES+4 };
static unsigned char *arena;static bool occupied[HOST_PAGES],pmm_ready,release_fail,duplicate,unaligned;
static unsigned live,acquires,releases;static int budget=-1;
static unsigned char *page_at(unsigned i) { return arena+(size_t)i*2*PAGE_SIZE; }
bool page_alloc(uint64_t *out)
{
    ++acquires;if (!out || !pmm_ready || budget==0) { return false; }
    if (duplicate || unaligned) { *out=(uintptr_t)page_at(0)+(unaligned ? 1 : 0);return true; }
    for (unsigned i=0;i<HOST_PAGES;++i) { if (!occupied[i]) { occupied[i]=true;++live;if (budget>0) { --budget; }memset(page_at(i),0xa5,PAGE_SIZE);*out=(uintptr_t)page_at(i);return true; } }return false;
}
bool page_free(uint64_t physical)
{
    ++releases;if (release_fail) { return false; }
    for (unsigned i=0;i<HOST_PAGES;++i) { if ((uintptr_t)page_at(i)==physical && occupied[i]) { for (unsigned j=0;j<PAGE_SIZE;++j) { assert(!page_at(i)[j]); }occupied[i]=false;--live;return true; } }return false;
}
struct page_stats pages_stats(void) { return (struct page_stats){.ready=pmm_ready,.free_pages=HOST_PAGES-live,.allocated_pages=live}; }
static void empty(void) { struct heap_stats h=heap_get_stats();assert(!live && !h.pages && !h.objects && !h.capacity_bytes && heap_check()); }
static void boundaries(void)
{
    assert(!kmalloc(1) && !heap_init());pmm_ready=true;assert(heap_init() && heap_check() && !acquires && kfree(NULL));
    assert(!kmalloc(0) && !kmalloc(4097) && !kmalloc(SIZE_MAX) && !kcalloc(SIZE_MAX,16) && !kcalloc(16,SIZE_MAX) && !kcalloc(0,1) && !kcalloc(1,0));
    for (unsigned size=1;size<=4096;++size) {
        unsigned char *p=kmalloc(size);assert(p && !((uintptr_t)p&15));size_t capacity=heap_usable_size(p);assert(capacity>=size && capacity<=4096 && !(capacity&(capacity-1)));
        assert(heap_init() && heap_usable_size(p)==capacity);for (unsigned i=0;i<capacity;++i) { assert(!p[i]);p[i]=(unsigned char)(i+1); }
        assert(!kfree(p+1) && !heap_usable_size(p+1));for (unsigned i=0;i<capacity;++i) { assert(p[i]==(unsigned char)(i+1)); }
        assert(kfree(p) && !kfree(p) && !heap_usable_size(p));empty();
    }
    /* Foreign and completely inaccessible addresses must never be read. */
    assert(!kfree(arena+PAGE_SIZE) && !kfree((void *)(uintptr_t)1) && !kfree((void *)UINTPTR_MAX));
    assert(!krealloc(arena+PAGE_SIZE,10) && !heap_usable_size(arena+PAGE_SIZE));
    puts("PASS: every size 1..4096, zeroing/alignment/classes, no eager pages, idempotent initialization, overflow/invalid/interior/double/guard-pointer rejection and page wiping/reclamation");
}
static void packing(void)
{
    void *p[257];for (unsigned i=0;i<257;++i) { p[i]=kmalloc(1);assert(p[i]);for (unsigned j=0;j<i;++j) { assert(p[i]!=p[j]); }memset(p[i],(unsigned char)i,16); }
    assert(live==2 && heap_get_stats().objects==257 && heap_get_stats().capacity_bytes==257*16 && heap_check());
    for (unsigned i=0;i<256;i+=2) { assert(kfree(p[i]) && !kfree(p[i]));p[i]=NULL; }
    for (unsigned i=1;i<257;i+=2) { for (unsigned j=0;j<16;++j) { assert(((unsigned char *)p[i])[j]==(unsigned char)i); } }
    for (unsigned i=0;i<257;++i) { if (p[i]) { assert(kfree(p[i])); } }empty();
    for (unsigned cls=0;cls<HEAP_CLASSES;++cls) {
        unsigned size=16u<<cls,count=4096/size;for (unsigned i=0;i<count;++i) { p[i]=kmalloc(size);assert(p[i]); }assert(live==1 && heap_check());
        for (unsigned i=0;i<count;++i) { assert(kfree(p[i])); }empty();
    }
    puts("PASS: all nine classes pack a full page, 256 small objects share a page, freed slots preserve neighbors and empty class pages return to PMM");
}
static void failures(void)
{
    unsigned char *p=kmalloc(17);assert(p);memset(p,0x5a,32);unsigned oldlive=live;budget=0;
    assert(!krealloc(p,4096) && heap_usable_size(p)==32 && live==oldlive);for (unsigned i=0;i<32;++i) { assert(p[i]==0x5a); }
    assert(krealloc(p,1)==p && heap_usable_size(p)==32 && !krealloc(p,4097));budget=-1;
    unsigned char *grown=krealloc(p,257);assert(grown && heap_usable_size(grown)==512 && !heap_usable_size(p));
    for (unsigned i=0;i<512;++i) { assert(grown[i]==(i<32 ? 0x5a : 0)); }assert(!krealloc(grown,0));empty();
    p=krealloc(NULL,1);assert(p && kfree(p));p=kcalloc(7,9);assert(p && heap_usable_size(p)==64);for (unsigned i=0;i<64;++i) { assert(!p[i]); }assert(kfree(p));empty();
    void *full[HEAP_MAX_PAGES];for (unsigned i=0;i<HEAP_MAX_PAGES;++i) { full[i]=kmalloc(4096);assert(full[i]); }
    unsigned before=acquires;struct heap_stats snapshot=heap_get_stats();assert(!kmalloc(4096) && !kmalloc(1) && acquires==before && heap_check());
    struct heap_stats after=heap_get_stats();assert(after.objects==snapshot.objects && after.pages==snapshot.pages && after.capacity_bytes==snapshot.capacity_bytes);
    for (unsigned i=0;i<HEAP_MAX_PAGES;++i) { assert(kfree(full[i])); }empty();
    budget=0;assert(!kmalloc(1) && !live && heap_check());budget=-1;
    puts("PASS: realloc/calloc data preservation and fresh zeroed capacity, failed growth atomicity, shrink, descriptor/PMM exhaustion, overflow and successful recovery without leaked pages");
}
static void random_cases(void)
{
    struct object { unsigned char *p;size_t capacity;unsigned char pattern; } slots[384]={0};uint32_t random=0x1278ab34;
    for (unsigned iteration=0;iteration<40000;++iteration) {
        random=random*1664525u+1013904223u;unsigned index=(random>>8)%384;struct object *o=&slots[index];
        if (o->p) { assert(heap_usable_size(o->p)==o->capacity);for (unsigned j=0;j<o->capacity;++j) { assert(o->p[j]==o->pattern); } }
        random=random*1664525u+1013904223u;size_t bytes=1+(random%4096);
        if (o->p && (random&3)==0) { assert(kfree(o->p));*o=(struct object){0}; }
        else {
            unsigned char *out=o->p ? krealloc(o->p,bytes) : kmalloc(bytes);
            if (out) {
                size_t cap=heap_usable_size(out);assert(cap>=bytes);
                for (unsigned j=0;j<384;++j) { if (j!=index && slots[j].p) { uintptr_t a=(uintptr_t)out,b=(uintptr_t)slots[j].p;assert(a+cap<=b || b+slots[j].capacity<=a); } }
                if (o->p) { for (size_t j=0;j<o->capacity;++j) { assert(out[j]==o->pattern); } }
                else { for (size_t j=0;j<cap;++j) { assert(!out[j]); } }
                *o=(struct object){out,cap,(unsigned char)(iteration+1)};memset(out,o->pattern,cap);
            }else if (o->p) { assert(heap_usable_size(o->p)==o->capacity);for (size_t j=0;j<o->capacity;++j) { assert(o->p[j]==o->pattern); } }
        }
        if (!(iteration%127)) { assert(heap_check() && heap_get_stats().pages==live); }
    }
    for (unsigned i=0;i<384;++i) { if (slots[i].p) { assert(kfree(slots[i].p)); } }empty();
    puts("PASS: 40000 mixed allocation/grow/shrink/free operations, independent no-overlap/live-pattern model, bounded capacity and guard pages, no leaks or ownership-counter drift");
}
int main(int argc,char **argv)
{
    SYSTEM_INFO system;GetSystemInfo(&system);assert(system.dwPageSize==PAGE_SIZE);
    arena=VirtualAlloc(NULL,HOST_PAGES*2*PAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    for (unsigned i=0;i<HOST_PAGES;++i) { DWORD previous;assert(VirtualProtect(page_at(i)+PAGE_SIZE,PAGE_SIZE,PAGE_NOACCESS,&previous)); }
    if (argc==2) {
        pmm_ready=true;assert(heap_init());unsigned char *p=kmalloc(16);assert(p);memset(p,0x6b,16);
        if (!strcmp(argv[1],"release")) { release_fail=true;assert(!kfree(p)); }
        else { if (!strcmp(argv[1],"duplicate")) { duplicate=true; }else { assert(!strcmp(argv[1],"unaligned"));unaligned=true; }assert(!kmalloc(32));for (unsigned i=0;i<16;++i) { assert(p[i]==0x6b); } }
        struct heap_stats h=heap_get_stats();unsigned calls=acquires;assert(h.poisoned && h.objects==1 && h.pages==1 && !heap_check() && !heap_init() && !kmalloc(16) && acquires==calls && !kfree(p));
        printf("PASS: PMM %s failure stops heap access without clearing foreign/live memory or claiming successful reclamation\n",argv[1]);
    }else { assert(argc==1);boundaries();packing();failures();random_cases(); }
    assert(VirtualFree(arena,0,MEM_RELEASE));return 0;
}
