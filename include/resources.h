#ifndef OS_RESOURCES_H
#define OS_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum resource_kind { RESOURCE_MMIO, RESOURCE_DMA_MEMORY, RESOURCE_IRQ, RESOURCE_DMA_CHANNEL, RESOURCE_GPIO };
struct resource_range {
    enum resource_kind kind;
    uint64_t domain,start,length;
};
enum { RESOURCE_SLOTS=16 };
struct resource_claim {
    struct resource_range range;
    const char *owner;
    unsigned ticket;
};

/* Foreground kernel callers only. Supply a static owner label. Physical
 * spans use domain 0, numbered resources use their provider's identity. */
unsigned resource_acquire(const char *,const struct resource_range *,size_t);
bool resource_release(unsigned ticket);
bool resource_get(unsigned index,struct resource_claim *);
bool resource_contains(unsigned ticket,enum resource_kind, uint64_t domain,uint64_t start,uint64_t length);
void resource_report(void);
#endif
