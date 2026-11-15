#ifndef OUR_OS_ALLOCATOR_H
#define OUR_OS_ALLOCATOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { UOS_ALLOC_BLOCKS=64,UOS_ALLOC_GROUPS=16,UOS_ALLOC_PAGE=4096,UOS_ALLOC_MAX=65536,UOS_ALLOC_ALIGN=16 };
struct uos_alloc_group { uintptr_t base;uint32_t unit;uint16_t pages,live; };
struct uos_alloc_block { uintptr_t pointer;uint32_t bytes;uint16_t group,slot; };
/* Providers return a fresh, page-aligned, disjoint run or NULL. Release must
 * leave the run mapped/owned on failure and wipe/release it on success. */
struct uos_allocator_ops {
    void *(*allocate)(void *,unsigned);
    bool (*release)(void *,void *);
    void *context;
};
struct uos_allocator {
    struct uos_alloc_group groups[UOS_ALLOC_GROUPS];
    struct uos_alloc_block blocks[UOS_ALLOC_BLOCKS];
    struct uos_allocator_ops ops;
    uint64_t allocations,frees,resizes,failures;
    unsigned peak_pages;
    bool ready,poisoned;
};
struct uos_allocator_stats {
    size_t live_blocks,requested_bytes,capacity_bytes,pages,peak_pages;
    uint64_t allocations,frees,resizes,failures;
    bool ready,poisoned;
};
/* Initialize one zeroed, inactive context; destroy before reinitializing it.
 * State lives in caller-owned writable memory, never in executable globals. */
bool uos_allocator_setup(struct uos_allocator *,const struct uos_allocator_ops *);
bool uos_allocator_init(struct uos_allocator *);
void *uos_malloc(struct uos_allocator *,size_t);
void *uos_calloc(struct uos_allocator *,size_t,size_t);
void *uos_realloc(struct uos_allocator *,void *,size_t);
bool uos_free(struct uos_allocator *,void *);
bool uos_allocator_destroy(struct uos_allocator *);
struct uos_allocator_stats uos_allocator_stats(const struct uos_allocator *);
#endif
