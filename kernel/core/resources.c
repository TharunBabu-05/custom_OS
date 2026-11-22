#include "resources.h"

static struct resource_claim table[RESOURCE_SLOTS];
static unsigned last_ticket;

static bool valid(const struct resource_range *range)
{
    if ((unsigned)range->kind>RESOURCE_GPIO || !range->length ||
        range->length-1>UINT64_MAX-range->start) { return false; }
    if (range->kind<=RESOURCE_DMA_MEMORY) { return !range->domain; }
    return range->domain && range->start<=UINT32_MAX &&
        range->length-1<=UINT32_MAX-range->start;
}

static bool overlap(const struct resource_range *a,const struct resource_range *b)
{
    if (a->domain!=b->domain || (a->domain && a->kind!=b->kind)) { return false; }
    return a->start<=b->start ? b->start-a->start<a->length : a->start-b->start<b->length;
}

unsigned resource_acquire(const char *owner,const struct resource_range *request,size_t count)
{
    if (!owner || !*owner || !request || !count || count>RESOURCE_SLOTS ||
        last_ticket==UINT32_MAX) { return 0; }
    unsigned free=0;
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) { free+=!table[i].ticket; }
    if (count>free) { return 0; }

    for (size_t i=0;i<count;++i) {
        if (!valid(&request[i])) { return 0; }
        for (size_t j=0;j<i;++j) {
            if (overlap(&request[i],&request[j])) { return 0; }
        }
        for (unsigned j=0;j<RESOURCE_SLOTS;++j) {
            if (table[j].ticket && overlap(&request[i],&table[j].range)) { return 0; }
        }
    }

    unsigned ticket=++last_ticket,j=0;
    for (size_t i=0;i<count;++i) {
        while (table[j].ticket) { ++j; }
        table[j++]=(struct resource_claim){request[i],owner,ticket};
    }
    return ticket;
}

bool resource_release(unsigned ticket)
{
    if (!ticket) { return false; }
    bool released=false;
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        if (table[i].ticket==ticket) { table[i].ticket=0; released=true; }
    }
    return released;
}

bool resource_get(unsigned index,struct resource_claim *out)
{
    if (!out || index>=RESOURCE_SLOTS || !table[index].ticket) { return false; }
    *out=table[index];
    return true;
}

bool resource_contains(unsigned ticket,enum resource_kind kind,uint64_t domain,uint64_t start,uint64_t length)
{
    if (!ticket || !length) { return false; }
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        const struct resource_range *range=&table[i].range;
        if (table[i].ticket!=ticket || range->kind!=kind || range->domain!=domain ||
            start<range->start || length>range->length) { continue; }
        if (start-range->start<=range->length-length) { return true; }
    }
    return false;
}
