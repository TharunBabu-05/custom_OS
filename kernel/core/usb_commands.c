#include "usb.h"
#include "console.h"
#include "xhci_test.h"
static uintptr_t firmware_dtb,console_base;
static bool pi_board;
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
void usb_setup(const char *board,uintptr_t dtb,uintptr_t uart_base)
{
    pi_board=same(board,"rpi5-header") || same(board,"rpi5"); firmware_dtb=dtb; console_base=uart_base;
}
bool usb_storage_attach(struct block_device *device)
{ return platform_usb_storage_attach(firmware_dtb,console_base,device); }
void usb_storage_info(void) { xhci_storage_info(); }
#ifdef OUROS_PI5
#include "rp1_probe.h"
static bool mmio_read(void *context,uint32_t offset,uint32_t *value)
{
    uintptr_t base=(uintptr_t)context;
    __asm__ volatile("dmb sy" ::: "memory");
    *value=*(volatile uint32_t *)(base+offset);
    __asm__ volatile("dmb sy" ::: "memory"); return true;
}
static void pi5_report_ports(void)
{
    if (console_base!=RP1_LIVE_BASE+0x30000) { console_write("USB port snapshot requires the Pi 5 header mapping.\n"); return; }
    const struct xhci_observer bridge={mmio_read,(void *)RP1_PCIE_BASE,0x9310};
    for (unsigned host=0;host<2;++host) {
        struct pi5_usb_description d;
        struct xhci_snapshot snapshot;
        unsigned offset=0x200000+host*0x100000;
        console_write("USB host=usb"); console_u64(host); console_write("\n");
        if (pi5_usb_describe((const void *)firmware_dtb,0x200000,host,&d) &&
            (same(d.status,"okay") || same(d.status,"ok")) &&
            rp1_reachable(&bridge,offset+d.span-1)==RP1_SNAPSHOT_OK) {
            const struct xhci_observer io={mmio_read,(void *)(RP1_LIVE_BASE+offset),d.span};
            if (xhci_observe(&io,&snapshot)==USB_PROBE_OK) { xhci_report_ports(&io,&snapshot); continue; }
        }
        console_write("USB port snapshot unavailable.\n");
    }
}
static void print_snapshot(const struct xhci_snapshot *s)
{
    console_write("xHCI version="); console_hex(s->version);
    console_write(" slots="); console_u64(s->slots); console_write(" interrupters="); console_u64(s->interrupters);
    console_write(" ports="); console_u64(s->ports);
    console_write(s->address64 ? " address_bits=64" : " address_bits=32");
    console_write(s->context64 ? " context_bytes=64\n" : " context_bytes=32\n");
    console_write("USBCMD="); console_hex(s->command); console_write(" USBSTS="); console_hex(s->status);
    console_write(" PAGESIZE="); console_hex(s->page_sizes); console_write("\n");
    console_write((s->status&1) ? "Controller halted.\n" : "Controller not halted.\n");
    if (s->status&(1u<<11)) { console_write("Controller not ready.\n"); }
    if (s->status&((1u<<2)|(1u<<12))) { console_write("Controller reports an error; capture raw status.\n"); }
    for (unsigned i=0;i<s->ports;++i) {
        const struct usb_port_snapshot *p=&s->port[i];
        console_write("port="); console_u64(i+1); console_write(" PORTSC="); console_hex(p->status);
        console_write((p->status&1) ? " connected=yes" : " connected=no");
        console_write((p->status&2) ? " enabled=yes" : " enabled=no");
        console_write((p->status&512) ? " powered=yes" : " powered=no");
        console_write(" speed_id="); console_u64((p->status>>10)&15);
        console_write(" link_state="); console_u64((p->status>>5)&15);
        console_write(" protocol_revision=");
        if (p->protocol_major) {
            console_hex((unsigned)p->protocol_major<<8|p->protocol_minor);
        } else { console_write("unreported"); }
        console_write("\n");
    }
}
#endif
bool usb_command(const char *line)
{
    if (same(line,"usbdevices")) { xhci_device_inventory(); return true; }
    if (same(line,"usbports")) {
#ifdef OUROS_PI5
        pi5_report_ports();
#else
        xhci_storage_ports();
#endif
        return true;
    }
    bool keyboard=line[0]=='u' && line[1]=='s' && line[2]=='b' && line[3]=='k';
    const char *key=keyboard ? "usbkeytest" : "usbidentify",*arg=line;
    while (*key && *arg==*key) { ++arg; ++key; }
    if (!*key && (!*arg || *arg==' ')) {
        while (*arg==' ') { ++arg; }
        unsigned host=2;
        if (arg[0]=='u' && arg[1]=='s' && arg[2]=='b' && (arg[3]=='0' || arg[3]=='1') && arg[4]==' ') {
            host=(unsigned)(arg[3]-'0');
            arg+=5;
            while (*arg==' ') { ++arg; }
        }
        unsigned port=0;
        while (*arg>='0' && *arg<='9' && port<=USB_MAX_PORTS) { port=port*10+(unsigned)(*arg++-'0'); }
        while (*arg==' ') { ++arg; }
        if (*arg || !port || port>USB_MAX_PORTS) {
            console_write(keyboard ? "Usage: usbkeytest [usb0|usb1] <USB2 root port>\n" : "Usage: usbidentify [usb0|usb1] <USB2 root port>\n");
            return true;
        }
        const struct xhci_accessory accessory={port,keyboard};
        if (host==2) { xhci_accessory_active(&accessory); }
#ifdef OUROS_PI5
        else { pi5_usb_accessory(firmware_dtb,console_base,host,&accessory); }
#else
        else { console_write("Named RP1 USB controllers require a Raspberry Pi 5.\n"); }
#endif
        return true;
    }
    if (same(line,"usbcheck")) { (void)xhci_storage_check();return true; }
    if (same(line,"usbtest")) { platform_usb_test(firmware_dtb,console_base); return true; }
    if (same(line,"usbenum")) { platform_usb_enumerate(firmware_dtb,console_base); return true; }
    bool probe=same(line,"usbprobe"); if (!probe && !same(line,"usbinfo")) { return false; }
    console_write("USB: discovery diagnostics; storageinit enables experimental read-only USB2 BOT.\n");
    if (!pi_board) { console_write("No Pi RP1 USB controller on QEMU virt; no MMIO attempted.\n"); return true; }
    console_write("Booting from USB uses Pi firmware; the RAM root is independent of the pendrive.\n");
    console_write("usbinfo reads DT only; usbprobe reads xHCI registers without changing them.\n");
    bool mapping=console_base==0x1c00030000ULL;
    for (unsigned i=0;i<2;++i) {
        struct pi5_usb_description d;
        console_write("usb"); console_u64(i); console_write(": ");
        if (!pi5_usb_describe((const void *)firmware_dtb,0x200000,i,&d)) {
            console_write("No supported Pi 5 RP1 host description; no MMIO attempted.\n"); continue;
        }
        console_write(d.path); console_write("\nDT status="); console_write(d.status);
        console_write(" RP1 bus="); console_hex(d.bus_address); console_write(" span="); console_hex(d.span); console_write("\n");
        if (!mapping) { console_write("Firmware RP1 mapping not confirmed by header UART; probe disabled.\n"); continue; }
        uintptr_t base=console_base-0x30000+(uintptr_t)(d.bus_address-0xc040000000ULL);
        console_write("Firmware CPU window candidate="); console_hex(base); console_write("\n");
        if (!probe) { continue; }
        if (!same(d.status,"okay") && !same(d.status,"ok")) {
            console_write("DT host disabled; register probe skipped.\n"); continue;
        }
#ifdef OUROS_PI5
        struct xhci_observer io={mmio_read,(void *)base,d.span}; struct xhci_snapshot snapshot;
        console_write("Reading controller registers now.\n");
        enum usb_probe_result r=xhci_observe(&io,&snapshot);
        console_write(usb_probe_error(r)); console_write("\n");
        if (r==USB_PROBE_OK) { print_snapshot(&snapshot); }
#else
        console_write("Pi MMIO unavailable in this build.\n");
#endif
    }
    console_write("A connected port does not identify the pendrive or mount /boot.\n");
    console_write("On a fresh boot use storageinit first; usbtest/usbenum consume the one-shot controller session.\n");
    return true;
}
