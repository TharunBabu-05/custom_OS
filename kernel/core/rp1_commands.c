#include "console.h"
#include "rp1_probe.h"
#include "rp1_msix.h"

#ifdef OUROS_PI5
#include "mmu.h"
#include "resources.h"
#include "xhci_test.h"

static bool read_register(void *context,uint32_t offset,uint32_t *out)
{
    *out=*(const volatile uint32_t *)((uintptr_t)context+offset);
    __asm__ volatile("dmb sy" ::: "memory");
    return true;
}

static bool write_register(void *context,uint32_t offset,uint32_t value)
{
    *(volatile uint32_t *)((uintptr_t)context+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
    return true;
}

static void test_gpio(const struct xhci_observer *gpio,unsigned pin)
{
    const struct resource_range claim={RESOURCE_GPIO,RP1_GPIO_BASE,pin,1};
    unsigned ticket=resource_acquire("gpio-test",&claim,1);
    if (!ticket) { console_write("GPIO test refused: pin claim unavailable.\n"); return; }
    enum rp1_gpio_result result=rp1_gpio_test(gpio,write_register,pin);
    if (result!=RP1_GPIO_RESTORE) { (void)resource_release(ticket); }
    static const char *const report[]={
        [RP1_GPIO_OK]="PASS: GPIO RIO latch and enable verified, pad isolated, state and claim restored.\n",
        [RP1_GPIO_BUSY]="GPIO test refused: pin is not isolated.\n",
        [RP1_GPIO_IO]="GPIO test failed: register readback could not be verified.\n",
        [RP1_GPIO_RESTORE]="GPIO test failed: restore unverified, pin claim retained until reboot.\n"
    };
    console_write(report[result]);
}

static void report_gpio(const void *dtb,uintptr_t uart,unsigned pin,bool test)
{
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    const struct xhci_observer gpio={read_register,(void *)RP1_GPIO_BASE,RP1_GPIO_SPAN};
    if (uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() || !rp1_pci_description(dtb,0x200000) ||
        !rp1_gpio_description(dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_GPIO_OFFSET+RP1_GPIO_SPAN-1)!=RP1_SNAPSHOT_OK) {
        console_write("GPIO unavailable on this Pi 5 header profile.\n"); return;
    }
    if (test) { test_gpio(&gpio,pin); return; }
    struct rp1_gpio_snapshot snapshot;
    if (!rp1_gpio_read(&gpio,pin,&snapshot)) { console_write("GPIO readback failed.\n"); return; }
    static const char *const pulls[]={"off","down","up","both"};
    console_write("GPIO "); console_u64(pin);
    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        struct resource_claim claim;
        if (resource_get(i,&claim) && claim.range.kind==RESOURCE_GPIO && claim.range.domain==RP1_GPIO_BASE &&
            pin>=claim.range.start && pin-claim.range.start<claim.range.length) {
            console_write(" reserved="); console_write(claim.owner); break;
        }
    }
    console_write(" funcsel="); console_u64(snapshot.control&31);
    console_write(" pull="); console_write(pulls[snapshot.pad>>2&3]);
    console_write(" pad_input="); console_u64(snapshot.pad>>6&1);
    console_write(" pad_output_disable="); console_u64(snapshot.pad>>7&1);
    console_write(" oe_override="); console_u64(snapshot.control>>14&3);
    console_write(" rio_in="); console_u64(snapshot.input);
    console_write(" rio_out="); console_u64(snapshot.output);
    console_write(" rio_oe="); console_u64(snapshot.output_enable); console_write("\n");
}

static void report_msix(const void *dtb,uintptr_t uart)
{
    struct rp1_topology topology;
    struct rp1_mip mip;
    if (uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() ||
        !rp1_pci_description(dtb,0x200000) || !rp1_topology_read(dtb,0x200000,&topology) ||
        !rp1_mip_describe(dtb,0x200000,topology.msi_parent,&mip)) {
        console_write("RP1 MSI-X preflight requires the known Pi 5 header and device tree.\n");
        return;
    }
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    enum rp1_mip_aperture aperture=rp1_mip_aperture_read(&bridge);
    console_write("RP1 PCIe doorbell="); console_hex(mip.doorbell);
    console_write(aperture==RP1_MIP_MAPPED ? " inbound=mapped\n" :
                  aperture==RP1_MIP_UNMAPPED ? " inbound=unmapped\n" : " inbound=unverified\n");

    /* Reserve just the index and the first 256 bytes of the indirect aperture.
     * The xHCI storage path uses its live MMIO and never selects config space. */
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,RP1_PCIE_BASE+0x8000,0x100},
        {RESOURCE_MMIO,0,RP1_PCIE_BASE+0x9000,4}
    };
    unsigned ticket=resource_acquire("rp1-pci-observe",claims,2);
    if (!ticket) { console_write("RP1 PCI configuration window already claimed.\n"); return; }
    struct rp1_pci_header root;
    struct rp1_msix msix;
    enum rp1_pci_result result=rp1_msix_read(&bridge,write_register,&root,&msix);
    if (result!=RP1_PCI_RESTORE) { (void)resource_release(ticket); }
    if (result!=RP1_PCI_BRIDGE) {
        console_write("RP1 PCIe root link="); console_hex(root.link);
        console_write(" header="); console_hex(root.header>>16&0xff);
        console_write(" primary="); console_u64(root.buses&255);
        console_write(" secondary="); console_u64(root.buses>>8&255);
        console_write(" subordinate="); console_u64(root.buses>>16&255); console_write("\n");
    }
    if (result!=RP1_PCI_OK) {
        static const char *const failures[]={
            [RP1_PCI_BRIDGE]="bridge registers unreadable",
            [RP1_PCI_LINK]="root port or active link unverified",
            [RP1_PCI_BUS]="firmware secondary bus unverified",
            [RP1_PCI_SELECTOR]="selector unavailable or did not match",
            [RP1_PCI_ENDPOINT]="unknown endpoint or memory decoding off",
            [RP1_PCI_CAPABILITY]="MSI-X capability unavailable or malformed",
            [RP1_PCI_RESTORE]="selector restore unverified; claims retained"
        };
        console_write("RP1 MSI-X preflight: "); console_write(failures[result]); console_write("\n");
        return;
    }
    console_write("RP1 MSI-X bus="); console_u64(msix.bus);
    console_write(" capability="); console_hex(msix.capability);
    console_write(" entries="); console_u64(msix.entries);
    console_write(msix.control&0x8000 ? " enabled=yes" : " enabled=no");
    console_write(msix.control&0x4000 ? " function_mask=yes\n" : " function_mask=no\n");

    uint64_t table,pba,uart_cpu;
    unsigned pba_bytes=((msix.entries+63)/64)*8;
    bool translated=rp1_pci_cpu_address(&bridge,msix.peripheral_bus+0x30000,0x100,&uart_cpu) &&
                    uart_cpu==uart && rp1_pci_cpu_address(&bridge,msix.table_bus,msix.entries*16,&table) &&
                    rp1_pci_cpu_address(&bridge,msix.pba_bus,pba_bytes,&pba);
    console_write("RP1 MSI-X table_bus="); console_hex(msix.table_bus);
    console_write(" pba_bus="); console_hex(msix.pba_bus);
    if (translated && (table&~0xfffULL)!=(RP1_LIVE_BASE&~0xfffULL)) {
        console_write(" table_cpu="); console_hex(table);
        console_write(" pba_cpu="); console_hex(pba);
    } else { console_write(" cpu_mapping=unverified"); }
    console_write("\n");
}

static void report_dma(const struct xhci_observer *bridge)
{
    struct pi5_dma_window window;
    if (!pi5_dma_window_read(bridge,&window)) {
        console_write("RP1 DMA inbound window unavailable.\n"); return;
    }
    console_write("RP1 DMA inbound cpu="); console_hex(window.cpu);
    console_write(" pci="); console_hex(window.bus);
    console_write(" bytes="); console_u64(window.size); console_write("\n");

    for (unsigned i=0;i<RESOURCE_SLOTS;++i) {
        struct resource_claim claim;
        if (!resource_get(i,&claim) || claim.range.kind!=RESOURCE_DMA_MEMORY) { continue; }
        const struct resource_range *range=&claim.range;
        uint64_t bus;
        console_write("RP1 DMA claim owner="); console_write(claim.owner);
        console_write(" bytes="); console_u64(range->length);
        console_write(" bus=");
        if (mmu_dma_buffer(range->start,range->length) &&
            pi5_dma_address(&window,range->start,range->length,&bus)) { console_hex(bus); }
        else { console_write("unavailable"); }
        console_write("\n");
    }
}

static void report_mip(const void *dtb,const char *path)
{
    struct rp1_mip mip;
    struct rp1_mip_snapshot snapshot;
    const struct xhci_observer io={read_register,(void *)0x1000130000ULL,0xc0};
    if (!rp1_mip_describe(dtb,0x200000,path,&mip) || !rp1_mip_read(&io,&snapshot)) {
        console_write("RP1 MIP route or registers unavailable.\n"); return;
    }
    console_write("RP1 MIP doorbell="); console_hex(mip.doorbell);
    console_write(" first_gic_intid="); console_u64(mip.first_intid);
    console_write(" vectors="); console_u64(mip.vectors);
    console_write(" data_offset="); console_u64(mip.data_offset); console_write("\n");
    console_write("RP1 MIP edge="); console_hex(snapshot.edge);
    console_write(" host_mask="); console_hex(snapshot.host_mask);
    console_write(" vpu_mask="); console_hex(snapshot.vpu_mask);
    console_write(" host_pending="); console_hex(snapshot.host_pending); console_write("\n");
}

static void report_rp1(uintptr_t dtb,uintptr_t uart)
{
    struct rp1_topology topology;
    if (uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() ||
        !rp1_topology_read((const void *)dtb,0x200000,&topology)) {
        console_write("RP1 observation requires the known Pi 5 header UART, DT and memory profile.\n"); return;
    }
    console_write("RP1 MSI parent (DT): "); console_write(topology.msi_parent); console_write("\n");
    console_write("RP1 DMA engine channels (DT): ");
    if (topology.dma_channels) { console_u64(topology.dma_channels); }
    else { console_write("unavailable"); }
    console_write("\n");
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    const struct xhci_observer apbs={read_register,(void *)(RP1_LIVE_BASE+RP1_PCIE_APBS),0x110};
    struct rp1_irq_snapshot snapshot;
    enum rp1_snapshot_result result=rp1_irq_read(&bridge,&apbs,&topology,&snapshot);
    if (result!=RP1_SNAPSHOT_OK) {
        console_write(result==RP1_SNAPSHOT_LINK ? "RP1 PCIe link unavailable.\n" :
                      result==RP1_SNAPSHOT_WINDOW ? "RP1 outbound window unavailable.\n" :
                      "RP1 interrupt register read failed.\n");
        return;
    }
    console_write("RP1 PCIe link up, raw IRQ bits="); console_hex(snapshot.raw); console_write("\n");
    static const char *const names[]={"uart0","usb0","usb1","ethernet","dma"};
    for (unsigned i=0;i<RP1_ROUTE_COUNT;++i) {
        const struct rp1_route *route=&topology.routes[i];
        console_write("RP1 source="); console_write(names[i]);
        if (!route->trigger) { console_write(" route=unavailable\n"); continue; }
        console_write(" line="); console_u64(route->line);
        console_write(route->trigger==1 ? " rising-edge" : " high-level");
        console_write(snapshot.config[i]&1 ? " enabled=yes" : " enabled=no");
        console_write(snapshot.config[i]&8 ? " iack_enabled=yes" : " iack_enabled=no");
        console_write(snapshot.raw>>route->line&1 ? " raw=1\n" : " raw=0\n");
    }
    report_mip((const void *)dtb,topology.msi_parent);
    report_dma(&bridge);
}
#endif

bool rp1_gpio_command(const char *line,uintptr_t dtb,uintptr_t uart)
{
    const char *name="gpio";
    while (*name && *line==*name) { ++line; ++name; }
    if (*name) { return false; }
    bool test=*line=='t';
    name=test ? "test" : "info";
    while (*name && *line==*name) { ++line; ++name; }
    if (*name || (*line && *line!=' ')) { return false; }
    while (*line==' ') { ++line; }
    bool valid=*line>='0' && *line<='9';
    unsigned pin=0;
    while (*line>='0' && *line<='9' && pin<RP1_HEADER_GPIOS) {
        pin=pin*10+(unsigned)(*line++-'0');
    }
    while (*line==' ') { ++line; }
    if (!valid || *line || pin>=RP1_HEADER_GPIOS) {
        console_write(test ? "Usage: gpiotest <BCM GPIO 0..27>\n" : "Usage: gpioinfo <BCM GPIO 0..27>\n");
        return true;
    }
#ifdef OUROS_PI5
    report_gpio((const void *)dtb,uart,pin,test);
#else
    (void)dtb; (void)uart;
    console_write(test ? "GPIO test requires a Raspberry Pi 5.\n" : "GPIO readback requires a Raspberry Pi 5.\n");
#endif
    return true;
}

bool rp1_command(const char *line,uintptr_t dtb,uintptr_t uart)
{
    const char *name="rp1";
    while (*line && *line==*name) { ++line; ++name; }
    if (*name) { return false; }
    bool msix=line[0]=='m' && line[1]=='s' && line[2]=='i';
    if (msix) { line+=3; }
    if (!(line[0]=='i' && line[1]=='n' && line[2]=='f' && line[3]=='o' && !line[4])) { return false; }
#ifdef OUROS_PI5
    if (msix) { report_msix((const void *)dtb,uart); }
    else { report_rp1(dtb,uart); }
#else
    (void)dtb; (void)uart;
    console_write(msix ? "RP1 MSI-X preflight requires a Raspberry Pi 5.\n" :
                         "RP1 observation requires a Raspberry Pi 5.\n");
#endif
    return true;
}
