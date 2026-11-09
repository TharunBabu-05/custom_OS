#include "xhci_test.h"
#include "console.h"
#include "cpu.h"
static struct xhci_test_memory memory;
static struct xhci_test_state state;
static struct pi5_dma_window window;
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
{ (void)context; return pi5_dma_address(&window,(uintptr_t)address,length,bus); }
static bool run(uintptr_t dtb,uintptr_t uart_base,bool enumerate,struct block_device *storage)
{
    if (state.attempted) { xhci_test_report(XHCI_TEST_USED,&state); return false; }
    uint64_t sctlr; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    if (uart_base!=0x1c00030000ULL || (sctlr&5) || !pi5_dma_description((const void *)dtb,0x200000)) {
        console_write("USB test refused: requires known Pi header mapping, MMU/cache off and supported RP1 DMA description.\n"); return false;
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
    for (unsigned i=0;i<2 && !found;++i) {
        struct pi5_usb_description candidate; struct xhci_snapshot snap;
        if (!pi5_usb_describe((const void *)dtb,0x200000,i,&candidate) || !enabled(candidate.status)) { continue; }
        uintptr_t base=0x1c00200000ULL+(uintptr_t)i*0x100000;
        struct xhci_observer observer={rd,(void *)base,candidate.span};
        if (xhci_observe(&observer,&snap)!=USB_PROBE_OK) { continue; }
        for (unsigned port=0;port<snap.ports;++port) {
            if (snap.port[port].status&1) { chosen=i; found=true; break; }
        }
    }
    if (!pi5_usb_describe((const void *)dtb,0x200000,chosen,&d) || !enabled(d.status)) {
        console_write("USB test refused: selected host is not enabled in DT.\n"); return false;
    }
    console_write(storage ? "storageinit selected usb" : enumerate ? "usbenum selected usb" : "usbtest selected usb"); console_u64(chosen);
    console_write("; only this controller is reset. Firmware USB device state will be lost.\n");
    console_write(storage ? "Testing DMA, USB2 configuration and read-only SCSI; controller stays running on success.\n" : enumerate ? "Testing DMA, then reading USB2 descriptors; RAMFS and UART remain available.\n" :
        "Testing command/event DMA with four No-ops; RAMFS and UART remain available.\n");
    struct xhci_test_io io={{rd,(void *)(0x1c00200000ULL+(uintptr_t)chosen*0x100000),d.span},wr,dma,barrier,ticks,timer_frequency()};
    struct usb_device_info info;
    enum xhci_test_result r=storage ? xhci_storage_start(&io,&memory,&state,&info,storage) : enumerate ? xhci_device_test(&io,&memory,&state,&info) : xhci_ring_test(&io,&memory,&state);
    if (!storage || r!=XHCI_TEST_OK) { xhci_test_report(r,&state); }
    if (enumerate && r==XHCI_TEST_OK) { usb_device_report(&info); }
    if (storage) { xhci_storage_info(); }
    return r==XHCI_TEST_OK;
}
void platform_usb_test(uintptr_t dtb,uintptr_t uart_base) { (void)run(dtb,uart_base,false,NULL); }
void platform_usb_enumerate(uintptr_t dtb,uintptr_t uart_base) { (void)run(dtb,uart_base,true,NULL); }
bool platform_usb_storage_attach(uintptr_t dtb,uintptr_t uart_base,struct block_device *device)
{ return run(dtb,uart_base,true,device); }
