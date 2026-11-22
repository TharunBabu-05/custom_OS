#include "console.h"
#include "rp1_dma_test.h"

#ifdef OUROS_PI5
#include "cpu.h"
#include "mmu.h"
#include "resources.h"
#include "rp1_probe.h"
#include "xhci_test.h"

static _Alignas(64) struct dma_test_memory {
    struct rp1_dma_descriptor descriptor;
    uint8_t source[RP1_DMA_TEST_BYTES];
    uint8_t guard_before[RP1_DMA_TEST_BYTES];
    uint8_t destination[RP1_DMA_TEST_BYTES];
    uint8_t guard_after[RP1_DMA_TEST_BYTES];
} memory;
static unsigned retained;

static void barrier(void *context)
{
    (void)context;
    __asm__ volatile("dsb sy" ::: "memory");
}

static bool read(void *context,uint32_t offset,uint32_t *out)
{
    *out=*(volatile uint32_t *)((uintptr_t)context+offset);
    barrier(context);
    return true;
}

static bool write(void *context,unsigned offset,uint32_t value)
{
    *(volatile uint32_t *)((uintptr_t)context+offset)=value;
    barrier(context);
    return true;
}

static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }

static bool map(const struct pi5_dma_window *window,unsigned ticket,const void *buffer,size_t size,uint64_t *bus)
{
    uintptr_t physical=(uintptr_t)buffer;
    return resource_contains(ticket,RESOURCE_DMA_MEMORY,0,physical,size) &&
           mmu_dma_buffer(physical,size) && pi5_dma_address(window,physical,size,bus) &&
           !(*bus&63) && *bus<(1ULL<<39) && size<=(1ULL<<39)-*bus;
}

static void run(uintptr_t dtb,uintptr_t uart)
{
    if (retained) { console_write("RP1 DMA test unavailable: a previous run reserved the engine until reboot.\n"); return; }
    const struct xhci_observer bridge={read,(void *)RP1_PCIE_BASE,0x9310};
    struct pi5_dma_window window;
    if (uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() ||
        !rp1_dma_description((const void *)dtb,0x200000) || !pi5_dma_description((const void *)dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_DMA_OFFSET)!=RP1_SNAPSHOT_OK || !pi5_dma_window_read(&bridge,&window)) {
        console_write("RP1 DMA test unavailable: DT, memory or live PCIe preflight failed.\n"); return;
    }
    uint32_t clock;
    read((void *)(RP1_LIVE_BASE+0x18000),0x44,&clock);
    if (clock==UINT32_MAX || !(clock&(1u<<11))) {
        console_write("RP1 DMA test unavailable: firmware DMA clock is off or unreadable.\n"); return;
    }
    const uintptr_t base=RP1_LIVE_BASE+RP1_DMA_OFFSET;
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,base,0x1000},
        {RESOURCE_DMA_CHANNEL,base,RP1_DMA_TEST_CHANNEL,1},
        {RESOURCE_DMA_MEMORY,0,(uintptr_t)&memory,sizeof(memory)}
    };
    unsigned ticket=resource_acquire("rp1-dma-test",claims,3);
    if (!ticket) { console_write("RP1 DMA test unavailable: engine or buffer claim refused.\n"); return; }

    uint64_t descriptor,source,destination;
    if (!map(&window,ticket,&memory.descriptor,sizeof(memory.descriptor),&descriptor) ||
        !map(&window,ticket,memory.source,sizeof(memory.source),&source) ||
        !map(&window,ticket,memory.destination,sizeof(memory.destination),&destination)) {
        (void)resource_release(ticket);
        console_write("RP1 DMA test unavailable: scratch buffers failed DMA mapping.\n"); return;
    }
    unsigned seed=(unsigned)timer_ticks();
    for (unsigned i=0;i<RP1_DMA_TEST_BYTES;++i) {
        memory.source[i]=(uint8_t)(seed+i*29);
        memory.destination[i]=(uint8_t)~memory.source[i];
        memory.guard_before[i]=0xa5;
        memory.guard_after[i]=0x5a;
    }
    rp1_dma_descriptor_init(&memory.descriptor,source,destination);
    const struct rp1_dma_io io={(void *)base,read,write,ticks,barrier};
    struct rp1_dma_report report;
    enum rp1_dma_result result=rp1_dma_copy(&io,descriptor,timer_frequency()/4,&report);
    if (result==RP1_DMA_RETAIN) {
        retained=ticket;
    } else {
        (void)resource_release(ticket);
    }
    if (result==RP1_DMA_OK) {
        const volatile struct dma_test_memory *actual=&memory;
        for (unsigned i=0;i<RP1_DMA_TEST_BYTES;++i) {
            uint8_t expected=(uint8_t)(seed+i*29);
            if (actual->source[i]!=expected || actual->destination[i]!=expected ||
                actual->guard_before[i]!=0xa5 || actual->guard_after[i]!=0x5a) {
                console_write("FAIL: RP1 DMA data or guard mismatch; channel restored.\n"); return;
            }
        }
        console_write("PASS: RP1 DMA copied 64 bytes, guards intact, channel and claims restored.\n");
        return;
    }
    static const char *const failure[]={
        [RP1_DMA_BUSY]="engine busy or interrupt signaling enabled",
        [RP1_DMA_IO]="register setup or readback failed",
        [RP1_DMA_TIMEOUT]="completion timed out",
        [RP1_DMA_ERROR]="channel reported an error",
        [RP1_DMA_RETAIN]="cleanup unverified; claims reserved until reboot"
    };
    console_write("RP1 DMA test: "); console_write(failure[result]);
    console_write("; status="); console_hex(report.status);
    if (report.register_failed) {
        console_write(" reg="); console_hex(report.offset);
        console_write(" expected="); console_hex(report.expected);
        console_write(" observed="); console_hex(report.observed);
    }
    console_write("\n");
}
#endif

bool rp1_dma_test_command(const char *line,uintptr_t dtb,uintptr_t uart)
{
    const char *name="rp1dmatest";
    while (*line && *line==*name) { ++line; ++name; }
    if (*line || *name) { return false; }
#ifdef OUROS_PI5
    run(dtb,uart);
#else
    (void)dtb; (void)uart;
    console_write("RP1 DMA test requires a Raspberry Pi 5.\n");
#endif
    return true;
}
