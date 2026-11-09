/* QEMU-only PCI bootstrap for the optional qemu-xhci validation device. */
#include "xhci_test.h"
#include "console.h"
#include "cpu.h"
#include "dt_property.h"
static struct xhci_test_memory memory;
static struct xhci_test_state state;
static uint64_t be64(const unsigned char *p)
{ uint64_t value=0; for (unsigned i=0;i<8;++i) { value=value<<8|p[i]; } return value; }
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
static bool dma(void *context,const void *address,size_t length,uint64_t *bus)
{ (void)context; if (!length) { return false; } *bus=(uintptr_t)address; return true; }
static void barrier(void *context) { (void)context; __asm__ volatile("dsb sy" ::: "memory"); }
static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }
static bool run(uintptr_t dtb,uintptr_t uart_base,bool enumerate,struct block_device *storage)
{
    (void)uart_base;
    if (state.attempted) { xhci_test_report(XHCI_TEST_USED,&state); return false; }
    const unsigned char *p; uint32_t n;
    if (!dt_property((const void *)dtb,0x200000,"/pcie@10000000","reg",&p,&n) ||
        n!=16 || be64(p)!=0x4010000000ULL || be64(p+8)!=0x10000000) {
        console_write("QEMU xHCI test unavailable: unsupported ECAM layout.\n"); return false;
    }
    uintptr_t pci=0;
    for (unsigned slot=1;slot<32;++slot) {
        uintptr_t candidate=0x4010000000ULL+(slot<<15); uint32_t id,class;
        rd((void *)candidate,0,&id); rd((void *)candidate,8,&class);
        if (id==0x000d1b36 && (class>>8)==0x0c0330) { pci=candidate; break; }
    }
    if (!pci) { console_write("No qemu-xhci attached; use scripts/test-usb-qemu.ps1.\n"); return false; }
    uint32_t bar,command; rd((void *)pci,0x10,&bar); rd((void *)pci,4,&command);
    if ((bar&15)!=4) { console_write("QEMU xHCI BAR is not supported 64-bit memory.\n"); return false; }
    /* Only this known emulator device is configured. No bridge reset. */
    wr((void *)pci,4,command&0xffffu&~6u);
    wr((void *)pci,0x10,0x10000004); wr((void *)pci,0x14,0);
    wr((void *)pci,4,(command&0xffffu)|6u);
    console_write("QEMU qemu-xhci BAR0=0x10000000; command/event DMA test.\n");
    struct xhci_test_io io={{rd,(void *)0x10000000,0x4000},wr,dma,barrier,ticks,timer_frequency()};
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
