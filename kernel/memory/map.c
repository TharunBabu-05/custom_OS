#include "memory.h"

bool memory_claim_bitmap(struct memory_map *map,uint64_t *physical)
{
    if (!map || !physical || !map->ram_count || map->ram_count>MEMORY_MAX_RAM || map->reserved_count>=MEMORY_MAX_RESERVED) { return false; }
    uint64_t previous=0;
    for (unsigned i=0;i<map->ram_count;++i) {
        struct memory_range r=map->ram[i];if (!r.size || r.base>UINT64_MAX-r.size || (i && r.base<previous)) { return false; }previous=r.base+r.size;
    }
    for (unsigned i=0;i<map->reserved_count;++i) { struct memory_range r=map->reserved[i].range;if (!r.size || r.base>UINT64_MAX-r.size) { return false; } }
    for (unsigned i=0;i<map->ram_count;++i) {
        uint64_t at=map->ram[i].base,end=at+map->ram[i].size;if (end>PMM_ADDRESS_LIMIT) { end=PMM_ADDRESS_LIMIT; }
        if (at>UINT64_MAX-(PAGE_SIZE-1)) { continue; }at=(at+PAGE_SIZE-1)&~(PAGE_SIZE-1);if (!at) { at=PAGE_SIZE; }
        while (at<end) {
            uint64_t blocked=at,next=end;
            for (unsigned j=0;j<map->reserved_count;++j) {
                struct memory_range r=map->reserved[j].range;uint64_t stop=r.base+r.size;
                if (r.base<=at && stop>blocked) { blocked=stop; }else if (r.base>at && r.base<next) { next=r.base; }
            }
            if (blocked>at) { if (blocked>UINT64_MAX-(PAGE_SIZE-1)) { break; }at=(blocked+PAGE_SIZE-1)&~(PAGE_SIZE-1);continue; }
            if (next-at>=PMM_BITMAP_BYTES) {
                if (!memory_reserve(map,at,PMM_BITMAP_BYTES,"physical page bitmap")) { return false; }*physical=at;return true;
            }
            if (next>=end || next>UINT64_MAX-(PAGE_SIZE-1)) { break; }at=(next+PAGE_SIZE-1)&~(PAGE_SIZE-1);
        }
    }return false;
}

bool memory_reserve(struct memory_map *map, uint64_t base, uint64_t size, const char *reason)
{
    if (!map || !size || base > UINT64_MAX-size || !reason ||
        map->reserved_count >= MEMORY_MAX_RESERVED) { return false; }
    map->reserved[map->reserved_count++]=(struct memory_reservation){{base,size},reason};
    return true;
}
bool memory_normalize(struct memory_map *map)
{
    if (!map || !map->ram_count || map->ram_count > MEMORY_MAX_RAM ||
        map->reserved_count > MEMORY_MAX_RESERVED) { return false; }
    for (unsigned i=0; i<map->ram_count; ++i) {
        if (!map->ram[i].size || map->ram[i].base > UINT64_MAX-map->ram[i].size) { return false; }
        for (unsigned j=i; j && map->ram[j].base < map->ram[j-1].base; --j) {
            struct memory_range temp=map->ram[j]; map->ram[j]=map->ram[j-1]; map->ram[j-1]=temp;
        }
    }
    unsigned count=0;
    for (unsigned i=0; i<map->ram_count; ++i) {
        struct memory_range r=map->ram[i];
        if (count && r.base <= map->ram[count-1].base+map->ram[count-1].size) {
            uint64_t end=r.base+r.size, old_end=map->ram[count-1].base+map->ram[count-1].size;
            if (end > old_end) { map->ram[count-1].size=end-map->ram[count-1].base; }
        } else { map->ram[count++]=r; }
    }
    map->ram_count=count; map->ram_bytes=0;
    for (unsigned i=0; i<count; ++i) {
        if (map->ram_bytes > UINT64_MAX-map->ram[i].size) { return false; }
        map->ram_bytes+=map->ram[i].size;
    }
    for (unsigned i=0; i<map->reserved_count; ++i) {
        struct memory_range r=map->reserved[i].range;
        if (!r.size || r.base > UINT64_MAX-r.size) { return false; }
    }
    return true;
}
