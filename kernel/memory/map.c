#include "memory.h"

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
