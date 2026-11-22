#include "console.h"
#include "resources.h"

void resource_report(void)
{
    static const char *const names[]={"mmio","dma-memory","irq","dma-channel","gpio"};
    unsigned active=0;
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        struct resource_claim claim;
        if (!resource_get(i,&claim)) { continue; }
        const struct resource_range *range=&claim.range;
        console_write("owner="); console_write(claim.owner);
        console_write(" kind="); console_write(names[range->kind]);
        if (range->domain) { console_write(" domain="); console_hex(range->domain); }
        console_write(" start=");
        if (range->domain) { console_u64(range->start); }
        else { console_hex(range->start); }
        console_write(" length="); console_u64(range->length);
        console_write("\n");
        ++active;
    }
    console_write("Resource claims: "); console_u64(active);
    console_write("/"); console_u64(RESOURCE_SLOTS); console_write("\n");
}
