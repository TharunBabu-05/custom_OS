#include "memory.h"
#define BITMAP_WORDS (PMM_ADDRESS_LIMIT/PAGE_SIZE/64)
static uint64_t used[BITMAP_WORDS];
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
bool pages_init(const struct memory_map *map)
{
    stats=(struct page_stats){0}; usable_count=0; next_word=0;
    if (!map || !map->ram_count || map->ram_count > MEMORY_MAX_RAM ||
        map->reserved_count > MEMORY_MAX_RESERVED) { return false; }
    for (size_t i=0; i<BITMAP_WORDS; ++i) { used[i]=UINT64_MAX; }
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
    if (!physical || !stats.ready || !stats.free_pages) { return false; }
    size_t word=next_word;
    do {
        if (used[word] != UINT64_MAX) {
            uint64_t free_bits=~used[word]; unsigned bit=0;
            while (!(free_bits & (1ULL<<bit))) { ++bit; }
            used[word]|=1ULL<<bit;
            *physical=((uint64_t)word*64+bit)*PAGE_SIZE;
            next_word=word; --stats.free_pages; ++stats.allocated_pages;
            return true;
        }
        if (++word == BITMAP_WORDS) { word=0; }
    } while (word != next_word);
    return false;
}
bool page_free(uint64_t physical)
{
    if (!stats.ready || (physical & (PAGE_SIZE-1)) || physical >= PMM_ADDRESS_LIMIT) { return false; }
    bool managed=false;
    for (unsigned i=0; i<usable_count; ++i) {
        if (physical >= usable[i].base && physical-usable[i].base < usable[i].size) { managed=true; break; }
    }
    uint64_t page=physical/PAGE_SIZE;
    if (!managed || !(used[page/64] & (1ULL<<(page%64)))) { return false; }
    used[page/64]&=~(1ULL<<(page%64));
    if (page/64 < next_word) { next_word=(size_t)(page/64); }
    ++stats.free_pages; --stats.allocated_pages;
    return true;
}
struct page_stats pages_stats(void) { return stats; }
