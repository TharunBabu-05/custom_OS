#include "console.h"
#include "pi5_pcie.h"

#ifdef OUROS_PI5
static void report_windows(const void *dtb,const struct pi5_pcie_bridge *bridge,bool inbound)
{
    struct pi5_pcie_window windows[PI5_PCIE_MAX_WINDOWS];
    unsigned count=pi5_pcie_windows(dtb,0x200000,bridge,inbound,windows);
    const char *label=inbound ? "  inbound" : "  outbound";
    if (!count) {
        console_write(label);
        console_write(" windows unavailable\n");
    }
    for (unsigned i=0;i<count;++i) {
        const struct pi5_pcie_window *w=&windows[i];
        console_write(label);
        console_write(w->wide ? " mem64" : " mem32");
        if (w->prefetch) { console_write(" prefetch"); }
        console_write(" pci="); console_hex(w->pci);
        console_write(" cpu="); console_hex(w->cpu);
        console_write(" bytes="); console_hex(w->bytes);
        console_write("\n");
    }
}
#endif

void pi5_pcie_command(const void *dtb,enum pi5_pcie_port port,bool windows)
{
#ifdef OUROS_PI5
    struct pi5_pcie_bridge bridge;
    if (!pi5_pcie_describe(dtb,0x200000,port,&bridge)) {
        console_write("PCIe bridge device-tree binding unavailable.\n");
        return;
    }
    console_write(port==PI5_PCIE_EXTERNAL ? "PCIe external" : "PCIe RP1");
    console_write(bridge.state==DT_DISABLED ? " dt=disabled" : " dt=enabled");
    console_write(" domain="); console_u64(bridge.domain);
    console_write(" registers="); console_hex(bridge.registers);
    console_write(" lanes="); console_u64(bridge.lanes);
    console_write(" max_gen="); console_u64(bridge.generation);
    console_write("\n");
    if (windows) {
        report_windows(dtb,&bridge,false);
        report_windows(dtb,&bridge,true);
    }
#else
    (void)dtb; (void)port; (void)windows;
    console_write("PCIe inventory requires a Raspberry Pi 5.\n");
#endif
}
