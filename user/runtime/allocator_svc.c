#include "user.h"
#include "allocator.h"
static void *allocate(void *context,unsigned pages)
{ (void)context;int64_t address=uos_alloc_pages(pages);return address<0 ? NULL : (void *)(uintptr_t)address; }
static bool release(void *context,void *base)
{ (void)context;return uos_free_pages(base)==0; }
static bool resize(void *context,void *base,unsigned pages)
{ (void)context;return uos_resize_pages(base,pages)==0; }
bool uos_allocator_init(struct uos_allocator *allocator)
{
    const struct uos_allocator_ops ops={.allocate=allocate,.release=release,.resize=resize};
    return uos_allocator_setup(allocator,&ops);
}
