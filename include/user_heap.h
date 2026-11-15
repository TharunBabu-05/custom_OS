#ifndef OS_USER_HEAP_H
#define OS_USER_HEAP_H
#include "user_mmu.h"
struct user_heap {
    unsigned char runs[USER_HEAP_PAGES];
    unsigned live,peak;
    uint64_t allocations,free_calls;
    bool failed;
};
/* Called only while the user's root is inactive. Allocation yields a fresh,
 * compatible, zeroed owned page or no page on failure. Release wipes/frees a
 * known owned page. Publish must preserve the old tables on failure. */
struct user_heap_ops {
    bool (*allocate)(void *,uint64_t *);
    bool (*release)(void *,uint64_t);
    bool (*publish)(void *,const struct user_mapping *,unsigned);
    void *context;
};
enum user_heap_result { USER_HEAP_OK,USER_HEAP_ARGUMENT,USER_HEAP_NO_SPACE,
    USER_HEAP_NO_MEMORY,USER_HEAP_MAP_FAILED,USER_HEAP_CORRUPT };
/* Updates the map array/count only at publication. Failed PMM release is an
 * internal fatal condition: retain any unreleased pages for owner cleanup. */
enum user_heap_result user_heap_allocate(struct user_heap *,struct user_mapping *,unsigned *,
    unsigned,const struct user_heap_ops *,uint64_t *);
enum user_heap_result user_heap_free(struct user_heap *,struct user_mapping *,unsigned *,
    uint64_t,const struct user_heap_ops *);
#endif
