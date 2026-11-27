#include "xhci_test.h"
#include "console.h"
#include "cpu.h"
#include "mmu.h"
#include "resources.h"
#include "memory.h"
#include "rp1_probe.h"
enum { XHCI_MEMORY_PAGES=(sizeof(struct xhci_test_memory)+PAGE_SIZE-1)/PAGE_SIZE };
static struct xhci_test_state state;
static struct pi5_dma_window window,accessory_window;
static unsigned ticket;
static unsigned accessory_ticket;
static bool enabled(const char *s)
{ return s[0]=='o' && s[1]=='k' && (!s[2] || (s[2]=='a' && s[3]=='y' && !s[4])); }
static bool rd(void *context,uint32_t offset,uint32_t *value)
{
    __asm__ volatile("dmb sy" ::: "memory"); *value=*(volatile uint32_t *)((uintptr_t)context+offset);
    __asm__ volatile("dmb sy" ::: "memory"); return true;
}
static bool wr(void *context,uint32_t offset,uint32_t value)
{
    __asm__ volatile("dsb sy" ::: "memory"); *(volatile uint32_t *)((uintptr_t)context+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory"); return true;
}
static void barrier(void *context) { (void)context; __asm__ volatile("dsb sy" ::: "memory"); }
static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }
static bool dma(void *context,const void *address,size_t length,uint64_t *bus)
{
    (void)context;
    uintptr_t physical=(uintptr_t)address;
    return resource_contains(ticket,RESOURCE_DMA_MEMORY,0,physical,length) &&
           mmu_dma_buffer(physical,length) && pi5_dma_address(&window,physical,length,bus);
}
static bool accessory_dma(void *context,const void *address,size_t length,uint64_t *bus)
{
    (void)context;
    uintptr_t physical=(uintptr_t)address;
    return resource_contains(accessory_ticket,RESOURCE_DMA_MEMORY,0,physical,length) &&
           mmu_dma_buffer(physical,length) && pi5_dma_address(&accessory_window,physical,length,bus);
}

void pi5_usb_accessory(uintptr_t dtb,uintptr_t uart,unsigned host,const struct xhci_accessory *request)
{
    uintptr_t base=RP1_LIVE_BASE+0x200000+host*0x100000;
    if (xhci_storage_port(base)) { xhci_accessory_active(request); return; }
    struct pi5_usb_description d;
    struct xhci_observer bridge={rd,(void *)RP1_PCIE_BASE,0x9310};
    if (accessory_ticket || uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() ||
        !pi5_dma_description((const void *)dtb,0x200000) || !pi5_dma_window_read(&bridge,&accessory_window) ||
        !pi5_usb_describe((const void *)dtb,0x200000,host,&d) || !enabled(d.status) ||
        rp1_reachable(&bridge,(unsigned)(base-RP1_LIVE_BASE)+d.span-1)!=RP1_SNAPSHOT_OK) {
        console_write("USB accessory controller unavailable.\n"); return;
    }

    uint64_t physical;
    struct memory_range reachable={accessory_window.cpu,accessory_window.size};
    if (!page_alloc_window(XHCI_MEMORY_PAGES,1,0,reachable,&physical)) {
        console_write("USB accessory DMA window has no suitable free run.\n"); return;
    }
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,base,d.span},{RESOURCE_DMA_MEMORY,0,physical,sizeof(struct xhci_test_memory)}
    };
    accessory_ticket=resource_acquire("usb-accessory",claims,2);
    if (accessory_ticket) {
        struct xhci_test_io io={{rd,(void *)base,d.span},wr,accessory_dma,barrier,ticks,timer_frequency()};
        struct xhci_test_state probe={0};
        enum xhci_test_result result=xhci_accessory_test(&io,(void *)(uintptr_t)physical,&probe,request);
        if (result!=XHCI_TEST_OK) {
            console_write("USB accessory test ended: "); console_write(xhci_test_error(result));
            console_write(" phase="); console_u64(probe.phase); console_write("\n");
        }
        if (probe.attempted && !probe.halted) {
            console_write("USB accessory controller halt unverified. DMA retained until reboot.\n"); return;
        }
        (void)resource_release(accessory_ticket);
        accessory_ticket=0;
    } else { console_write("USB accessory controller is already claimed.\n"); }
    (void)page_free_run(physical,XHCI_MEMORY_PAGES);
}
static bool run(uintptr_t dtb,uintptr_t uart_base,bool enumerate,struct block_device *storage)
{
    if (state.attempted || ticket) { xhci_test_report(XHCI_TEST_USED,&state); return false; }
    if (uart_base!=0x1c00030000ULL || !mmu_dma_compatible() || !pi5_dma_description((const void *)dtb,0x200000)) {
        console_write("USB test refused: requires known Pi header mapping, caches off, MMU off/verified identity profile and supported RP1 DMA description.\n"); return false;
    }
    struct xhci_observer bridge={rd,(void *)0x1000120000ULL,0x9310};
    bool valid=pi5_dma_window_read(&bridge,&window);
    console_write("Live PCIe BAR2="); console_hex((uint64_t)window.bar_hi<<32|window.bar_lo);
    console_write(" UBUS_REMAP="); console_hex((uint64_t)window.remap_hi<<32|window.remap_lo); console_write("\n");
    if (!valid) { console_write("USB test refused: unsupported live inbound DMA mapping; no controller changes.\n"); return false; }
    console_write("DMA window CPU="); console_hex(window.cpu); console_write(" RP1/PCIe="); console_hex(window.bus);
    console_write(" bytes="); console_u64(window.size); console_write("\n");
    struct pi5_usb_description d; unsigned chosen=0; bool found=false;
    /* Choose the host with a connected root port; retain usb0 as fallback. */
    for (unsigned i=0;!storage && i<2 && !found;++i) {
        struct pi5_usb_description candidate; struct xhci_snapshot snap;
        if (!pi5_usb_describe((const void *)dtb,0x200000,i,&candidate) || !enabled(candidate.status)) { continue; }
        uintptr_t base=0x1c00200000ULL+(uintptr_t)i*0x100000;
        struct xhci_observer observer={rd,(void *)base,candidate.span};
        if (xhci_observe(&observer,&snap)!=USB_PROBE_OK) { continue; }
        for (unsigned port=0;port<snap.ports;++port) {
            if (snap.port[port].status&1) { chosen=i; found=true; break; }
        }
    }
    unsigned end=storage ? 2u : chosen+1;
    uint64_t physical=0;
    for (;chosen<end;++chosen) {
        if (!pi5_usb_describe((const void *)dtb,0x200000,chosen,&d) || !enabled(d.status) ||
            rp1_reachable(&bridge,0x200000+chosen*0x100000+d.span-1)!=RP1_SNAPSHOT_OK) {
            if (storage) { continue; }
            console_write("USB test refused: selected host unavailable.\n"); break;
        }
        if (!physical && !page_alloc_window(XHCI_MEMORY_PAGES,1,0,(struct memory_range){window.cpu,window.size},&physical)) {
            console_write("USB storage DMA window has no suitable free run.\n");break;
        }
        struct xhci_test_memory *memory=(void *)(uintptr_t)physical;
        uintptr_t base=0x1c00200000ULL+(uintptr_t)chosen*0x100000;
        const struct resource_range claims[]={
            {RESOURCE_MMIO,0,base,d.span},
            {RESOURCE_DMA_MEMORY,0,physical,sizeof(*memory)}
        };
        ticket=resource_acquire(chosen ? "usb1-xhci" : "usb0-xhci",claims,2);
        if (!ticket) {
            console_write("USB test refused: resource claim unavailable.\n");break;
        }
        console_write(storage ? "storageinit checking usb" : enumerate ? "usbenum selected usb" : "usbtest selected usb");
        console_u64(chosen); console_write("\n");
        struct xhci_test_io io={{rd,(void *)base,d.span},wr,dma,barrier,ticks,timer_frequency()};
        struct usb_device_info info;
        enum xhci_test_result r=storage ? xhci_storage_start(&io,memory,&state,&info,storage) : enumerate ? xhci_device_test(&io,memory,&state,&info) : xhci_ring_test(&io,memory,&state);
        bool try_next=storage && r==XHCI_TEST_NO_BOT && (!state.attempted || state.halted);
        if (!state.attempted || try_next) {
            if (!resource_release(ticket)) { console_write("USB DMA claim retained until reboot.\n");return false; }
            ticket=0;
        }
        if (try_next) { state=(struct xhci_test_state){0}; continue; }
        if (storage && r==XHCI_TEST_OK) {
            console_write("storageinit selected usb"); console_u64(chosen);
            console_write(" port="); console_u64(info.port); console_write(" (USB2 BOT).\n");
        }
        if (!storage || r!=XHCI_TEST_OK) { xhci_test_report(r,&state); }
        if (enumerate && r==XHCI_TEST_OK) { usb_device_report(&info); }
        if (storage) { xhci_storage_info(); }
        if (!ticket) { (void)page_free_run(physical,XHCI_MEMORY_PAGES); }
        return r==XHCI_TEST_OK;
    }
    if (physical) { (void)page_free_run(physical,XHCI_MEMORY_PAGES); }
    console_write("No supported direct USB2 BOT storage on either RP1 host.\n");
    return false;
}
void platform_usb_test(uintptr_t dtb,uintptr_t uart_base) { (void)run(dtb,uart_base,false,NULL); }
void platform_usb_enumerate(uintptr_t dtb,uintptr_t uart_base) { (void)run(dtb,uart_base,true,NULL); }
bool platform_usb_storage_attach(uintptr_t dtb,uintptr_t uart_base,struct block_device *device)
{ return run(dtb,uart_base,true,device); }
