/* Original bounded page-backed size-class heap. All ownership metadata stays
 * outside client buffers; invalid pointers are compared, never dereferenced. */
#include "heap.h"
#include "memory.h"
struct heap_page { uintptr_t base;uint64_t bits[4];unsigned size,live; };
static struct heap_page pool[HEAP_MAX_PAGES];
static struct heap_stats stats;
static void wipe(void *p,size_t bytes) { volatile unsigned char *v=p;while (bytes--) { *v++=0; } }
static unsigned class_size(size_t n)
{ if (!n || n>HEAP_MAX_ALLOCATION) { return 0; }unsigned size=HEAP_ALIGNMENT;while (size<n) { size*=2; }return size; }
static unsigned class_index(unsigned size)
{ unsigned i=0;while (size>HEAP_ALIGNMENT) { size/=2;++i; }return i; }
static struct heap_page *owner(const void *p,unsigned *slot)
{
    uintptr_t address=(uintptr_t)p;
    if (!stats.ready || stats.poisoned || !address) { return NULL; }
    for (unsigned i=0;i<HEAP_MAX_PAGES;++i) {
        struct heap_page *page=&pool[i];
        if (page->base && address>=page->base && address-page->base<PAGE_SIZE) {
            if (page->size<HEAP_ALIGNMENT || class_size(page->size)!=page->size) { stats.poisoned=true;return NULL; }
            uintptr_t offset=address-page->base;if (offset%page->size) { return NULL; }unsigned bit=(unsigned)(offset/page->size);
            if (!(page->bits[bit/64]&(1ULL<<(bit%64)))) { return NULL; }*slot=bit;return page;
        }
    }return NULL;
}
bool heap_init(void)
{
    if (stats.ready) { return !stats.poisoned; }
    if (!pages_stats().ready) { return false; }
    for (unsigned i=0;i<HEAP_CLASSES;++i) { stats.classes[i].size=HEAP_ALIGNMENT<<i; }stats.ready=true;return true;
}
void *kmalloc(size_t bytes)
{
    unsigned size=class_size(bytes);
    if (!stats.ready || stats.poisoned || !size) { ++stats.failures;return NULL; }
    struct heap_page *page=NULL,*empty=NULL;
    for (unsigned i=0;i<HEAP_MAX_PAGES;++i) {
        if (!pool[i].base && !empty) { empty=&pool[i]; }
        if (pool[i].base && pool[i].size==size && pool[i].live<PAGE_SIZE/size) { page=&pool[i];break; }
    }
    unsigned cls=class_index(size);
    if (!page) {
        uint64_t physical=0;
        if (!empty || !page_alloc(&physical)) { ++stats.failures;return NULL; }
        bool valid=physical && physical<=UINTPTR_MAX-(PAGE_SIZE-1) && !(physical&(PAGE_SIZE-1));
        for (unsigned i=0;i<HEAP_MAX_PAGES;++i) { if (pool[i].base==physical) { valid=false; } }
        /* A broken PMM must not let us clear a foreign/live page or release
         * it twice. Retain diagnostics and stop subsequent heap operations. */
        if (!valid) { stats.poisoned=true;++stats.failures;return NULL; }
        page=empty;*page=(struct heap_page){.base=(uintptr_t)physical,.size=size};wipe((void *)page->base,PAGE_SIZE);
        ++stats.pages;++stats.classes[cls].pages;if (stats.pages>stats.peak_pages) { stats.peak_pages=stats.pages; }
    }
    unsigned slots=(unsigned)PAGE_SIZE/size,slot=0;
    while (slot<slots && (page->bits[slot/64]&(1ULL<<(slot%64)))) { ++slot; }
    if (slot==slots) { stats.poisoned=true;++stats.failures;return NULL; }
    void *out=(void *)(page->base+(uintptr_t)slot*size);wipe(out,size);page->bits[slot/64]|=1ULL<<(slot%64);++page->live;
    ++stats.objects;++stats.classes[cls].objects;stats.capacity_bytes+=size;++stats.allocations;
    if (stats.objects>stats.peak_objects) { stats.peak_objects=stats.objects; }return out;
}
void *kcalloc(size_t count,size_t bytes)
{
    if (!count || !bytes || count>HEAP_MAX_ALLOCATION || bytes>HEAP_MAX_ALLOCATION/count) { ++stats.failures;return NULL; }
    return kmalloc(count*bytes);
}
size_t heap_usable_size(const void *p)
{ unsigned slot;struct heap_page *page=owner(p,&slot);return page ? page->size : 0; }
bool kfree(void *p)
{
    if (!p) { return true; }unsigned slot;struct heap_page *page=owner(p,&slot);
    if (!page) { ++stats.rejected_frees;return false; }
    unsigned size=page->size,cls=class_index(size);wipe(p,size);
    if (page->live==1) {
        wipe((void *)page->base,PAGE_SIZE);
        if (!page_free(page->base)) { stats.poisoned=true;++stats.rejected_frees;return false; }
        *page=(struct heap_page){0};--stats.pages;--stats.classes[cls].pages;
    }else { page->bits[slot/64]&=~(1ULL<<(slot%64));--page->live; }
    --stats.objects;--stats.classes[cls].objects;stats.capacity_bytes-=size;++stats.frees;return true;
}
void *krealloc(void *p,size_t bytes)
{
    if (!p) { return kmalloc(bytes); }
    unsigned slot;struct heap_page *page=owner(p,&slot);
    if (!page) { ++stats.failures;return NULL; }
    if (!bytes) { (void)kfree(p);return NULL; }
    if (bytes<=page->size) { return p; }
    size_t old=page->size;void *out=kmalloc(bytes);if (!out) { return NULL; }
    for (size_t i=0;i<old;++i) { ((unsigned char *)out)[i]=((const unsigned char *)p)[i]; }
    /* A PMM release error poisons the entire heap; no recovery/ownership
     * transfer is claimed in that internal-corruption case. */
    if (!kfree(p)) { return NULL; }return out;
}
struct heap_stats heap_get_stats(void) { return stats; }
bool heap_check(void)
{
    if (!stats.ready || stats.poisoned) { return false; }
    unsigned pages=0,objects=0;size_t bytes=0;struct heap_class_stats cls[HEAP_CLASSES]={0};
    for (unsigned i=0;i<HEAP_MAX_PAGES;++i) {
        const struct heap_page *page=&pool[i];
        if (!page->base) { if (page->size || page->live || page->bits[0] || page->bits[1] || page->bits[2] || page->bits[3]) { return false; }continue; }
        if ((page->base&(PAGE_SIZE-1)) || page->base>UINTPTR_MAX-(PAGE_SIZE-1) || page->size<HEAP_ALIGNMENT || class_size(page->size)!=page->size) { return false; }
        for (unsigned j=0;j<i;++j) { if (pool[j].base==page->base) { return false; } }
        unsigned live=0,slots=(unsigned)PAGE_SIZE/page->size;
        for (unsigned bit=0;bit<256;++bit) { if (page->bits[bit/64]&(1ULL<<(bit%64))) { if (bit>=slots) { return false; }++live; } }
        if (!live || live!=page->live) { return false; }
        unsigned c=class_index(page->size);++pages;objects+=live;bytes+=(size_t)live*page->size;++cls[c].pages;cls[c].objects+=live;
    }
    for (unsigned i=0;i<HEAP_CLASSES;++i) { if (stats.classes[i].size!=(unsigned)(HEAP_ALIGNMENT<<i) || cls[i].pages!=stats.classes[i].pages || cls[i].objects!=stats.classes[i].objects) { return false; } }
    return pages==stats.pages && objects==stats.objects && bytes==stats.capacity_bytes && stats.allocations-stats.frees==objects;
}
