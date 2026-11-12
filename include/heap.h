#ifndef OS_HEAP_H
#define OS_HEAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { HEAP_ALIGNMENT=16, HEAP_MAX_ALLOCATION=4096, HEAP_MAX_PAGES=128, HEAP_CLASSES=9 };
struct heap_class_stats { unsigned size,pages,objects; };
struct heap_stats {
    bool ready,poisoned;
    unsigned pages,objects,peak_pages,peak_objects;
    size_t capacity_bytes;
    uint64_t allocations,frees,failures,rejected_frees;
    struct heap_class_stats classes[HEAP_CLASSES];
};
/* Single-core, synchronous kernel buffers; not IRQ/SMP safe or a DMA API.
 * Pages come from the PMM and use identity addresses (MMU off or identity profile).
 * Repeated init preserves all live allocations. No allocations at init. */
bool heap_init(void);
/* 1..4096 bytes, rounded to a power-of-two class, 16-byte alignment.
 * New allocations (including spare capacity) are zeroed. */
void *kmalloc(size_t bytes);
void *kcalloc(size_t count,size_t bytes);
/* Growth failure preserves the old allocation; shrink retains its class.
 * realloc(NULL,n) allocates; realloc(p,0) frees and returns NULL. */
void *krealloc(void *pointer,size_t bytes);
/* NULL is accepted. Only a live allocation's exact base pointer is accepted.
 * Freed storage is wiped; completely empty pages return to the PMM. */
bool kfree(void *pointer);
size_t heap_usable_size(const void *pointer);
struct heap_stats heap_get_stats(void);
bool heap_check(void);
bool heap_command(const char *line);
#endif
