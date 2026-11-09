#include "xhci_test.h"
#include "console.h"
#include "cpu.h"
static struct xhci_test_memory memory;
static struct xhci_test_state state;
static struct pi5_dma_window window;
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
void platform_usb_test(uintptr_t dtb,uintptr_t uart_base)
{
    if (state.attempted) { xhci_test_report(XHCI_TEST_USED,&state); return; }
    uint64_t sctlr; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    if (uart_base!=0x1c00030000ULL || (sctlr&5) || !pi5_dma_description((const void *)dtb,0x200000)) {
        console_write("USB test refused: requires known Pi header mapping, MMU/cache off and supported RP1 DMA description.\n"); return;
    }
    struct xhci_observer bridge={rd,(void *)0x1000120000ULL,0x9310};
    bool valid=pi5_dma_window_read(&bridge,&window);
    console_write("Live PCIe BAR2="); console_hex((uint64_t)window.bar_hi<<32|window.bar_lo);
    console_write(" UBUS_REMAP="); console_hex((uint64_t)window.remap_hi<<32|window.remap_lo); console_write("\n");
    if (!valid) { console_write("USB test refused: unsupported live inbound DMA mapping; no controller changes.\n"); return; }
    console_write("DMA window CPU="); console_hex(window.cpu); console_write(" RP1/PCIe="); console_hex(window.bus);
    console_write(" bytes="); console_u64(window.size); console_write("\n");
    struct pi5_usb_description d;
    if (!pi5_usb_describe((const void *)dtb,0x200000,0,&d) ||
        !(d.status[0]=='o' && d.status[1]=='k' && (!d.status[2] || (d.status[2]=='a' && d.status[3]=='y' && !d.status[4])))) {
        console_write("USB test refused: usb0 is not enabled in DT.\n"); return;
    }
    console_write("usbtest takes over usb0 only; firmware USB device state will be lost.\n");
    console_write("Testing command/event DMA with four No-ops; RAMFS and UART remain available.\n");
    struct xhci_test_io io={{rd,(void *)0x1c00200000ULL,d.span},wr,dma,barrier,ticks,timer_frequency()};
    enum xhci_test_result r=xhci_ring_test(&io,&memory,&state); xhci_test_report(r,&state);
}
