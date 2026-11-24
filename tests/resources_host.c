#include "resources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned count_claims(void)
{
    struct resource_claim claim;
    unsigned count=0;
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) { count+=resource_get(i,&claim); }
    return count;
}

int main(void)
{
    const struct resource_range uart={RESOURCE_MMIO,0,0x2000,0x100};
    unsigned console=resource_acquire("console",&uart,1);
    assert(console && count_claims()==1);

    const struct resource_range rejected[]={
        {RESOURCE_IRQ,0x4000,30,1},
        {RESOURCE_DMA_MEMORY,0,0x1fff,2}
    };
    assert(!resource_acquire("overlap",rejected,2) && count_claims()==1);
    const struct resource_range repeated[]={uart,uart};
    assert(!resource_acquire("internal overlap",repeated,2) && count_claims()==1);
    const struct resource_range invalid[]={
        {RESOURCE_MMIO,0,UINT64_MAX,2},
        {RESOURCE_MMIO,4,0,1},
        {RESOURCE_MMIO,0,0,0},
        {RESOURCE_IRQ,0,30,1},
        {RESOURCE_IRQ,1,UINT32_MAX,2},
        {RESOURCE_DMA_CHANNEL,2,UINT64_MAX,1},
        {(enum resource_kind)99,1,3,1}
    };
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        assert(!resource_acquire("invalid",&invalid[i],1));
    }

    const struct resource_range different[]={
        {RESOURCE_MMIO,0,0x2100,0x400},
        {RESOURCE_MMIO,0,UINT64_MAX,1},
        {RESOURCE_IRQ,0x4000,30,1},
        {RESOURCE_IRQ,0x5000,30,1},
        {RESOURCE_DMA_CHANNEL,0x4000,30,1}
    };
    unsigned group=resource_acquire("driver",different,sizeof(different)/sizeof(*different));
    assert(group && group!=console && count_claims()==6);
    assert(!resource_acquire("another driver",different+2,1));
    struct resource_claim entry;
    assert(resource_get(0,&entry) && !strcmp(entry.owner,"console") && entry.range.start==0x2000);
    assert(!resource_get(0,NULL) && !resource_get(RESOURCE_SLOTS,&entry));
    assert(resource_release(group) && !resource_release(group) && count_claims()==1);

    struct resource_range slots[RESOURCE_SLOTS];
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        slots[i]=(struct resource_range){RESOURCE_DMA_CHANNEL,0x8000,i,1};
    }
    assert(!resource_acquire("too many",slots,RESOURCE_SLOTS) && count_claims()==1);
    unsigned full=resource_acquire("fills table",slots,RESOURCE_SLOTS-1);
    assert(full && count_claims()==RESOURCE_SLOTS);
    assert(!resource_acquire("too late",slots+RESOURCE_SLOTS-1,1));
    assert(!resource_release(group) && !resource_release(0) && count_claims()==RESOURCE_SLOTS);
    assert(resource_release(full) && resource_release(console) && count_claims()==0);
    const struct resource_range buffers[]={
        {RESOURCE_DMA_MEMORY,0,0x4000,0x100},{RESOURCE_DMA_MEMORY,0,0x4100,0x80}
    };
    unsigned dma=resource_acquire("dma",buffers,2);
    assert(dma && resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x4000,0x100));
    assert(resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x417f,1));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x40ff,2));
    assert(!resource_contains(dma,RESOURCE_MMIO,0,0x4000,1));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,1,0x4000,1));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x417f,2));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x4180,1));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x4000,0));
    assert(!resource_contains(dma,RESOURCE_DMA_MEMORY,0,UINT64_MAX,2));
    assert(resource_release(dma) && !resource_contains(dma,RESOURCE_DMA_MEMORY,0,0x4000,1));
    const struct resource_range serial_pins={RESOURCE_GPIO,0xd0000,14,2};
    const struct resource_range pin15={RESOURCE_GPIO,0xd0000,15,1};
    unsigned serial=resource_acquire("serial-console",&serial_pins,1);
    assert(serial && !resource_acquire("gpio-test",&pin15,1));
    const struct resource_range other_gpio={RESOURCE_GPIO,0xe0000,15,1};
    const struct resource_range gpio_irq={RESOURCE_IRQ,0xd0000,15,1};
    const struct resource_range pin17={RESOURCE_GPIO,0xd0000,17,1};
    assert(resource_acquire("other controller",&other_gpio,1));
    assert(resource_acquire("interrupt",&gpio_irq,1));
    unsigned gpio=resource_acquire("gpio-test",&pin17,1);
    assert(gpio && resource_release(gpio) && !resource_acquire("gpio-test",&pin15,1));
    const struct resource_range conflict[]={pin17,pin15};
    assert(!resource_acquire("gpio-loop",conflict,2) && count_claims()==3);
    const struct resource_range pair[]={pin17,{RESOURCE_GPIO,0xd0000,27,1}};
    unsigned loop=resource_acquire("gpio-loop",pair,2);
    assert(loop && count_claims()==5);
    assert(!resource_acquire("other loop",pair+1,1) && resource_release(loop) && count_claims()==3);
    puts("PASS: atomic resource claims, disjoint domains, span validation, grouped release and stale tickets.");
    return 0;
}
