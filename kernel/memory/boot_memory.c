#include "memory.h"
#include "console.h"
extern unsigned char __kernel_start[], __kernel_end[];
static struct memory_map boot_map;
static const char *status="not initialized";
static bool map_valid;
void memory_boot_init(uintptr_t dtb)
{
    /* Firmware-provided pointer is trusted to reference readable RAM. The
     * reader bounds every DTB section against this 2 MiB early-boot contract. */
    if (!dtb || (dtb&7) || !fdt_read_memory((const void *)dtb,0x200000,&boot_map)) {
        status="invalid or unsupported DTB memory/reservations; allocator disabled"; return;
    }
    uint64_t kernel_start=(uintptr_t)__kernel_start, kernel_end=(uintptr_t)__kernel_end;
    uint64_t first=boot_map.ram[0].base, guard=boot_map.ram[0].size;
    if (guard > 0x200000) { guard=0x200000; }
    if (kernel_end <= kernel_start ||
        !memory_reserve(&boot_map,first,guard,"boot guard (first 2 MiB)") ||
        !memory_reserve(&boot_map,kernel_start,kernel_end-kernel_start,"kernel/BSS/stack/bitmap") ||
        !memory_reserve(&boot_map,dtb,boot_map.dtb_size,"DTB")) {
        status="boot reservation limit or overflow; allocator disabled"; return;
    }
    map_valid=true;
    if (!pages_init(&boot_map)) { status="unsupported or exhausted usable RAM; allocator disabled"; return; }
    status="ready";
}
void memory_print_info(void)
{
    console_write("Memory: "); console_write(status); console_write("\n");
    if (!map_valid) { return; }
    struct page_stats s=pages_stats();
    console_write("Reported RAM: "); console_memory_size(boot_map.ram_bytes);
    console_write("\nManaged RAM:  "); console_memory_size(s.managed_pages*PAGE_SIZE);
    console_write("\nFree RAM:     "); console_memory_size(s.free_pages*PAGE_SIZE);
    console_write("\nAllocated:    "); console_memory_size(s.allocated_pages*PAGE_SIZE);
    console_write("\nUnavailable:  "); console_memory_size(boot_map.ram_bytes-s.managed_pages*PAGE_SIZE);
    console_write("\nMB=1,000,000 bytes; MiB=1,048,576 bytes (fractions truncated).\n");
    console_write("DTB RAM banks="); console_u64(boot_map.ram_count);
    console_write(" bytes="); console_u64(boot_map.ram_bytes);
    console_write(" (firmware-described, not free memory)\n");
    console_write("page_size=4096 address_limit_bytes="); console_u64(PMM_ADDRESS_LIMIT);
    console_write("\nmanaged_pages="); console_u64(s.managed_pages);
    console_write(" free_pages="); console_u64(s.free_pages);
    console_write(" allocated_pages="); console_u64(s.allocated_pages);
    console_write("\nmanaged_bytes="); console_u64(s.managed_pages*PAGE_SIZE);
    console_write(" unavailable_bytes="); console_u64(boot_map.ram_bytes-s.managed_pages*PAGE_SIZE);
    console_write(" (reservations, alignment, address limit)\n");
    console_write("usable_ranges="); console_u64(s.usable_ranges);
    console_write(" reservations="); console_u64(boot_map.reserved_count);
    console_write("\nPhysical pages only; MMU/heap not implemented.\n");
}
void memory_print_map(void)
{
    if (!map_valid) { memory_print_info(); return; }
    for (unsigned i=0; i<boot_map.ram_count; ++i) {
        console_write("RAM base="); console_hex(boot_map.ram[i].base);
        console_write(" bytes="); console_u64(boot_map.ram[i].size); console_write("\n");
    }
    for (unsigned i=0; i<boot_map.reserved_count; ++i) {
        console_write("RESERVED base="); console_hex(boot_map.reserved[i].range.base);
        console_write(" bytes="); console_u64(boot_map.reserved[i].range.size);
        console_write(" reason="); console_write(boot_map.reserved[i].reason); console_write("\n");
    }
}
bool memory_page_test(void)
{
    uint64_t addresses[4]={0}; unsigned count=0;
    struct page_stats before=pages_stats();
    if (!before.ready) { console_write("Page test unavailable: allocator disabled.\n"); return false; }
    bool good=true;
    for (; count<4; ++count) { if (!page_alloc(&addresses[count])) { good=false; break; } }
    /* Touch only pages acquired by this test. Patterns differ across pages
     * and offsets to detect overlapping allocation and basic RAM corruption. */
    for (unsigned i=0; i<count; ++i) {
        volatile uint64_t *page=(volatile uint64_t *)(uintptr_t)addresses[i];
        for (unsigned j=0; j<PAGE_SIZE/8; ++j) { page[j]=0x6f75726f73000000ULL ^ addresses[i] ^ j; }
    }
    for (unsigned i=0; i<count; ++i) {
        volatile uint64_t *page=(volatile uint64_t *)(uintptr_t)addresses[i];
        for (unsigned j=0; j<PAGE_SIZE/8; ++j) {
            if (page[j] != (0x6f75726f73000000ULL ^ addresses[i] ^ j)) { good=false; }
        }
    }
    for (unsigned i=0; i<count; ++i) { if (!page_free(addresses[i])) { good=false; } }
    struct page_stats after=pages_stats();
    good=good && after.free_pages == before.free_pages && after.allocated_pages == before.allocated_pages;
    console_write(good ? "PASS: four distinct pages written/read/freed; counters restored.\n" : "FAIL: physical page test.\n");
    return good;
}
