#include "allocator.h"
static void wipe(void *p,size_t n) { volatile unsigned char *out=p;while (n--) { *out++=0; } }
static void copy(void *d,const void *s,size_t n) { unsigned char *out=d;const unsigned char *in=s;while (n--) { *out++=*in++; } }
static void increment(uint64_t *n) { if (*n!=UINT64_MAX) { ++*n; } }
static void failure(struct uos_allocator *a) { if (a && a->ready) { increment(&a->failures); } }
static bool active(const struct uos_allocator *a) { return a && a->ready && !a->poisoned; }
static unsigned page_count(const struct uos_allocator *a)
{ unsigned n=0;for (unsigned i=0;i<UOS_ALLOC_GROUPS;++i) { if (a->groups[i].base) { n+=a->groups[i].pages; } }return n; }
static int block_id(const struct uos_allocator *a,const void *p)
{
    for (unsigned i=0;i<UOS_ALLOC_BLOCKS;++i) { if (a->blocks[i].pointer && a->blocks[i].pointer==(uintptr_t)p) { return (int)i; } }return -1;
}
static bool block_valid(const struct uos_allocator *a,unsigned id)
{
    const struct uos_alloc_block *b=&a->blocks[id];if (!b->pointer || b->group>=UOS_ALLOC_GROUPS || !b->bytes) { return false; }
    const struct uos_alloc_group *g=&a->groups[b->group];
    return g->base && g->pages && g->pages<=16 && g->unit>=16 && g->unit<=UOS_ALLOC_MAX && g->live &&
        b->bytes<=g->unit && b->slot<(unsigned)g->pages*UOS_ALLOC_PAGE/g->unit && b->pointer==g->base+(uintptr_t)b->slot*g->unit;
}
static bool vacant(const struct uos_allocator *a,unsigned group,unsigned slot)
{
    for (unsigned i=0;i<UOS_ALLOC_BLOCKS;++i) { if (a->blocks[i].pointer && a->blocks[i].group==group && a->blocks[i].slot==slot) { return false; } }return true;
}
bool uos_allocator_setup(struct uos_allocator *a,const struct uos_allocator_ops *ops)
{
    if (!a || a->ready || !ops || !ops->allocate || !ops->release) { return false; }
    struct uos_allocator_ops supplied=*ops;wipe(a,sizeof(*a));a->ops=supplied;a->ready=true;return true;
}
void *uos_malloc(struct uos_allocator *a,size_t bytes)
{
    if (!active(a) || !bytes || bytes>UOS_ALLOC_MAX) { failure(a);return NULL; }
    unsigned id=0;while (id<UOS_ALLOC_BLOCKS && a->blocks[id].pointer) { ++id; }
    if (id==UOS_ALLOC_BLOCKS) { failure(a);return NULL; }
    unsigned unit=16,pages=1;
    if (bytes<=2048) { while (unit<bytes) { unit*=2; } }else { pages=(unsigned)((bytes+4095)/4096);unit=pages*4096; }
    unsigned group=UOS_ALLOC_GROUPS,slot=0;
    for (unsigned i=0;i<UOS_ALLOC_GROUPS && group==UOS_ALLOC_GROUPS;++i) {
        const struct uos_alloc_group *g=&a->groups[i];if (!g->base || g->unit!=unit || g->pages!=pages) { continue; }
        unsigned slots=pages*4096/unit;
        for (unsigned j=0;j<slots;++j) { if (vacant(a,i,j)) { group=i;slot=j;break; } }
    }
    if (group==UOS_ALLOC_GROUPS) {
        for (unsigned i=0;i<UOS_ALLOC_GROUPS;++i) { if (!a->groups[i].base) { group=i;break; } }
        if (group==UOS_ALLOC_GROUPS || page_count(a)>16-pages) { failure(a);return NULL; }
        void *base=a->ops.allocate(a->ops.context,pages);if (!base) { failure(a);return NULL; }
        uintptr_t start=(uintptr_t)base;bool geometry=!(start&4095) && start<=UINTPTR_MAX-pages*4096;
        for (unsigned i=0;i<UOS_ALLOC_GROUPS && geometry;++i) {
            const struct uos_alloc_group *g=&a->groups[i];if (g->base && start<g->base+g->pages*4096 && g->base<start+pages*4096) { geometry=false; }
        }
        if (!geometry) { a->poisoned=true;failure(a);return NULL; }
        a->groups[group]=(struct uos_alloc_group){start,unit,(uint16_t)pages,0};
        unsigned live=page_count(a);if (live>a->peak_pages) { a->peak_pages=live; }
    }
    struct uos_alloc_group *g=&a->groups[group];uintptr_t pointer=g->base+(uintptr_t)slot*unit;
    wipe((void *)pointer,unit);a->blocks[id]=(struct uos_alloc_block){pointer,(uint32_t)bytes,(uint16_t)group,(uint16_t)slot};
    ++g->live;increment(&a->allocations);return (void *)pointer;
}
void *uos_calloc(struct uos_allocator *a,size_t count,size_t bytes)
{
    if (count && bytes>SIZE_MAX/count) { failure(a);return NULL; }return uos_malloc(a,count*bytes);
}
bool uos_free(struct uos_allocator *a,void *p)
{
    if (!p) { return true; }if (!active(a)) { failure(a);return false; }
    int id=block_id(a,p);if (id<0 || !block_valid(a,(unsigned)id)) { failure(a);return false; }
    struct uos_alloc_block *b=&a->blocks[id];struct uos_alloc_group *g=&a->groups[b->group];
    if (g->live==1) {
        if (!a->ops.release(a->ops.context,(void *)g->base)) { failure(a);return false; }*g=(struct uos_alloc_group){0};
    }else { wipe(p,g->unit);--g->live; }
    *b=(struct uos_alloc_block){0};increment(&a->frees);return true;
}
void *uos_realloc(struct uos_allocator *a,void *p,size_t bytes)
{
    if (!p) { return uos_malloc(a,bytes); }if (!active(a)) { failure(a);return NULL; }
    int id=block_id(a,p);if (id<0 || !block_valid(a,(unsigned)id) || bytes>UOS_ALLOC_MAX) { failure(a);return NULL; }
    if (!bytes) { (void)uos_free(a,p);return NULL; }
    struct uos_alloc_block *b=&a->blocks[id];unsigned previous=b->bytes;
    if (bytes<=a->groups[b->group].unit) {
        if (bytes>previous) { wipe((unsigned char *)p+previous,bytes-previous); }
        else { wipe((unsigned char *)p+bytes,previous-bytes); }b->bytes=(uint32_t)bytes;increment(&a->resizes);return p;
    }
    void *next=uos_malloc(a,bytes);if (!next) { return NULL; }copy(next,p,previous);
    if (!uos_free(a,p)) {
        if (!uos_free(a,next)) { a->poisoned=true; }return NULL;
    }
    increment(&a->resizes);return next;
}
bool uos_allocator_destroy(struct uos_allocator *a)
{
    if (!a) { return false; }if (!a->ready) { return true; }bool good=true;
    for (unsigned i=0;i<UOS_ALLOC_GROUPS;++i) {
        struct uos_alloc_group *g=&a->groups[i];if (!g->base) { continue; }
        if (!a->ops.release(a->ops.context,(void *)g->base)) { good=false;continue; }
        for (unsigned j=0;j<UOS_ALLOC_BLOCKS;++j) { if (a->blocks[j].pointer && a->blocks[j].group==i) { a->blocks[j]=(struct uos_alloc_block){0}; } }
        *g=(struct uos_alloc_group){0};
    }
    if (good) { wipe(a,sizeof(*a)); }else { a->poisoned=true;failure(a); }return good;
}
struct uos_allocator_stats uos_allocator_stats(const struct uos_allocator *a)
{
    struct uos_allocator_stats s={0};if (!a || !a->ready) { return s; }
    s.ready=true;s.poisoned=a->poisoned;s.peak_pages=a->peak_pages;s.allocations=a->allocations;s.frees=a->frees;s.resizes=a->resizes;s.failures=a->failures;
    s.pages=page_count(a);
    for (unsigned i=0;i<UOS_ALLOC_BLOCKS;++i) { if (a->blocks[i].pointer) {
        ++s.live_blocks;s.requested_bytes+=a->blocks[i].bytes;
        if (a->blocks[i].group<UOS_ALLOC_GROUPS) { s.capacity_bytes+=a->groups[a->blocks[i].group].unit; }
    } }return s;
}
