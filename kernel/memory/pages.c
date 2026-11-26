#include "memory.h"
#define BITMAP_WORDS (PMM_ADDRESS_LIMIT/PAGE_SIZE/64)
static uint64_t *used;
static struct memory_range usable[MEMORY_MAX_USABLE];
static struct page_stats stats;
static unsigned usable_count;
static size_t next_word;
static uint64_t down(uint64_t n) { return n & ~(PAGE_SIZE-1); }
static uint64_t up(uint64_t n) { return n > UINT64_MAX-(PAGE_SIZE-1) ? UINT64_MAX : down(n+PAGE_SIZE-1); }
static bool add_usable(uint64_t start, uint64_t end)
{
    if (start >= end) { return true; }
    if (usable_count == MEMORY_MAX_USABLE) { return false; }
    usable[usable_count++]=(struct memory_range){start,end-start};
    stats.managed_pages+=(end-start)/PAGE_SIZE;
    for (uint64_t page=start/PAGE_SIZE; page<end/PAGE_SIZE; ++page) {
        used[page/64]&=~(1ULL<<(page%64));
    }
    return true;
}
bool pages_init(const struct memory_map *map,void *bitmap,size_t bytes)
{
    stats=(struct page_stats){0}; usable_count=0; next_word=0;used=NULL;
    if (!bitmap || ((uintptr_t)bitmap&7) || bytes<PMM_BITMAP_BYTES || !map || !map->ram_count || map->ram_count > MEMORY_MAX_RAM ||
        map->reserved_count > MEMORY_MAX_RESERVED) { return false; }
    /* Input must be normalized; reject overlap instead of double-counting it. */
    uint64_t previous_end=0, total=0;
    for (unsigned i=0; i<map->ram_count; ++i) {
        struct memory_range r=map->ram[i];
        if (!r.size || r.base > UINT64_MAX-r.size || (i && r.base < previous_end) ||
            total > UINT64_MAX-r.size) { return false; }
        previous_end=r.base+r.size; total+=r.size;
    }
    for (unsigned i=0; i<map->reserved_count; ++i) {
        struct memory_range r=map->reserved[i].range;
        if (!r.size || r.base > UINT64_MAX-r.size) { return false; }
    }
    used=bitmap;for (size_t i=0;i<BITMAP_WORDS;++i) { used[i]=UINT64_MAX; }
    for (unsigned i=0; i<map->ram_count; ++i) {
        uint64_t at=up(map->ram[i].base), end=down(map->ram[i].base+map->ram[i].size);
        if (end > PMM_ADDRESS_LIMIT) { end=PMM_ADDRESS_LIMIT; }
        while (at < end) {
            uint64_t blocked_end=at, next=end;
            for (unsigned j=0; j<map->reserved_count; ++j) {
                struct memory_range r=map->reserved[j].range;
                uint64_t start=down(r.base), stop=up(r.base+r.size);
                if (start <= at && stop > blocked_end) { blocked_end=stop; }
                else if (start > at && start < next) { next=start; }
            }
            if (blocked_end > at) { at=blocked_end; continue; }
            if (!add_usable(at,next)) { return false; }
            at=next;
        }
    }
    stats.ram_bytes=total;
    stats.free_pages=stats.managed_pages;
    stats.usable_ranges=usable_count;
    stats.ready=stats.free_pages != 0;
    return stats.ready;
}
bool page_alloc(uint64_t *physical)
{
    if (!physical || !stats.ready) { return false; }
    if (!stats.free_pages) { ++stats.exhausted_allocations;return false; }
    size_t word=next_word;
    do {
        if (used[word] != UINT64_MAX) {
            uint64_t free_bits=~used[word]; unsigned bit=0;
            while (!(free_bits & (1ULL<<bit))) { ++bit; }
            used[word]|=1ULL<<bit;
            *physical=((uint64_t)word*64+bit)*PAGE_SIZE;
            next_word=word; --stats.free_pages; ++stats.allocated_pages;
            if (stats.allocated_pages>stats.peak_allocated_pages) { stats.peak_allocated_pages=stats.allocated_pages; }
            return true;
        }
        if (++word == BITMAP_WORDS) { word=0; }
    } while (word != next_word);
    return false;
}
static bool window_pages(struct memory_range window,unsigned index,uint64_t *start,uint64_t *end)
{
    if (!window.size || window.base>=PMM_ADDRESS_LIMIT) { return false; }
    uint64_t limit=PMM_ADDRESS_LIMIT;
    if (window.size<limit-window.base) { limit=window.base+window.size; }
    uint64_t first=usable[index].base,stop=first+usable[index].size;
    *start=up(first>window.base ? first : window.base)/PAGE_SIZE;
    *end=(stop<limit ? stop : limit)/PAGE_SIZE;
    return *start<*end;
}
struct page_layout page_window_layout(struct memory_range window)
{
    struct page_layout layout={0};
    for (unsigned i=0;stats.ready && i<usable_count;++i) {
        uint64_t start,end,run=0;
        if (!window_pages(window,i,&start,&end)) { continue; }
        layout.managed+=end-start;
        for (uint64_t page=start;page<end;++page) {
            if (used[page/64]&(1ULL<<(page%64))) { run=0;continue; }
            if (!run) { ++layout.runs; }
            ++layout.free;
            if (++run>layout.largest) { layout.largest=run; }
        }
    }
    return layout;
}
bool page_alloc_window(unsigned count,unsigned alignment,unsigned boundary,
                       struct memory_range window,uint64_t *physical)
{
    if (!count || !physical || !alignment || (alignment&(alignment-1)) ||
        (boundary && ((boundary&(boundary-1)) || count>boundary)) ||
        alignment>PMM_ADDRESS_LIMIT/PAGE_SIZE || !stats.ready || count>stats.free_pages) { return false; }
    uint64_t mask=alignment-1;
    for (unsigned i=0;i<usable_count;++i) {
        uint64_t start,end;
        if (!window_pages(window,i,&start,&end)) { continue; }
        for (uint64_t first=(start+mask)&~mask;first<end && count<=end-first;) {
            if (boundary && (first&(boundary-1))+count>boundary) {
                first=(first+boundary)&~((uint64_t)boundary-1);
                continue;
            }
            uint64_t page=first,stop=first+count;
            while (page<stop && !(used[page/64]&(1ULL<<(page%64)))) { ++page; }
            if (page!=stop) {
                first=(page+1+mask)&~mask;
                continue;
            }
            for (page=first;page<stop;++page) { used[page/64]|=1ULL<<(page%64); }
            stats.free_pages-=count;
            stats.allocated_pages+=count;
            if (stats.allocated_pages>stats.peak_allocated_pages) { stats.peak_allocated_pages=stats.allocated_pages; }
            *physical=first*PAGE_SIZE;
            return true;
        }
    }
    ++stats.exhausted_allocations;
    return false;
}
bool page_alloc_aligned(unsigned count,unsigned alignment,uint64_t *physical)
{ return page_alloc_window(count,alignment,0,(struct memory_range){0,PMM_ADDRESS_LIMIT},physical); }
bool page_alloc_run(unsigned count,uint64_t *physical)
{ return page_alloc_aligned(count,1,physical); }

bool page_free_run(uint64_t physical,unsigned count)
{
    if (!stats.ready || !count || (physical&(PAGE_SIZE-1)) || physical>=PMM_ADDRESS_LIMIT ||
        count>(PMM_ADDRESS_LIMIT-physical)/PAGE_SIZE) { return false; }
    bool managed=false;
    uint64_t bytes=(uint64_t)count*PAGE_SIZE;
    for (unsigned i=0; i<usable_count; ++i) {
        if (physical>=usable[i].base && bytes<=usable[i].size && physical-usable[i].base<=usable[i].size-bytes) {
            managed=true;break;
        }
    }
    if (!managed) { return false; }
    uint64_t first=physical/PAGE_SIZE,stop=first+count;
    for (uint64_t page=first;page<stop;++page) {
        if (!(used[page/64]&(1ULL<<(page%64)))) { return false; }
    }
    for (uint64_t page=first;page<stop;++page) { used[page/64]&=~(1ULL<<(page%64)); }
    if (first/64<next_word) { next_word=(size_t)(first/64); }
    stats.free_pages+=count;
    stats.allocated_pages-=count;
    return true;
}
bool page_free(uint64_t physical)
{ return page_free_run(physical,1); }
struct page_stats pages_stats(void) { return stats; }
