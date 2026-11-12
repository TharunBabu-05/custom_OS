#ifndef OS_MEMORY_H
#define OS_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PMM_ADDRESS_LIMIT (32ULL * 1024 * 1024 * 1024)
#define PMM_BITMAP_BYTES (PMM_ADDRESS_LIMIT/PAGE_SIZE/8)
enum { MEMORY_MAX_RAM=16, MEMORY_MAX_RESERVED=64, MEMORY_MAX_USABLE=128 };
struct memory_range { uint64_t base, size; };
struct memory_reservation { struct memory_range range; const char *reason; };
struct memory_map {
    struct memory_range ram[MEMORY_MAX_RAM];
    struct memory_reservation reserved[MEMORY_MAX_RESERVED];
    unsigned ram_count, reserved_count;
    uint32_t dtb_size;
    uint64_t ram_bytes;
};
struct page_stats {
    uint64_t ram_bytes, managed_pages, free_pages, allocated_pages;
    unsigned usable_ranges;
    bool ready;
};
/* A bounded DTB v17 memory/reservation reader. Output changes only on success. */
bool fdt_read_memory(const void *blob, size_t available, struct memory_map *out);
bool memory_reserve(struct memory_map *map, uint64_t base, uint64_t size, const char *reason);
bool memory_normalize(struct memory_map *map);
/* Claim a page-aligned bitmap arena from normalized RAM, excluding every
 * reservation. Does not touch RAM; map/output change only on success. */
bool memory_claim_bitmap(struct memory_map *,uint64_t *physical);
/* Single-core physical pages; no MMU, synchronization or per-caller ownership. */
bool pages_init(const struct memory_map *map,void *bitmap,size_t bytes);
bool page_alloc(uint64_t *physical);
bool page_free(uint64_t physical);
struct page_stats pages_stats(void);
void memory_boot_init(uintptr_t dtb);
bool memory_boot_map(struct memory_map *out);
void memory_print_info(void);
void memory_print_map(void);
bool memory_page_test(void);
#endif
