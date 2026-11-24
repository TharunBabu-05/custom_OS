#include "console.h"
#include "rp1_probe.h"
#include "rp1_msix.h"

enum gpio_command { GPIO_READ, GPIO_TEST, GPIO_LOOP, GPIO_PULL };

#ifdef OUROS_PI5
#include "cpu.h"
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

static uint64_t ethernet_ticks(void *context) { (void)context; return timer_ticks(); }

static void test_gpio(const struct xhci_observer *gpio,unsigned pin,enum gpio_command command)
{
    bool loop=command==GPIO_LOOP;
    const struct resource_range claims[]={
        {RESOURCE_GPIO,RP1_GPIO_BASE,pin,1}, {RESOURCE_GPIO,RP1_GPIO_BASE,RP1_LOOP_RX,1}
    };
    unsigned ticket=resource_acquire(loop ? "gpio-loop" : command==GPIO_PULL ? "gpio-pull" : "gpio-test",
                                     claims,loop ? 2 : 1);
    if (!ticket) { console_write("GPIO test refused: pin claim unavailable.\n"); return; }
    struct rp1_gpio_sample sample;
    enum rp1_gpio_result result;
    if (loop) { result=rp1_gpio_loop(gpio,write_register,&sample); }
    else if (command==GPIO_PULL) { result=rp1_gpio_pull(gpio,write_register,pin,&sample); }
    else { result=rp1_gpio_test(gpio,write_register,pin); }
    if (result!=RP1_GPIO_RESTORE) { (void)resource_release(ticket); }
    if (loop && result==RP1_GPIO_OK) {
        console_write("PASS: GPIO 17 to 27 loopback levels 01010, both pins and claims restored.\n"); return;
    }
    if (command==GPIO_PULL && result==RP1_GPIO_OK) {
        console_write("PASS: GPIO "); console_u64(pin);
        console_write(" input pull levels 010, pad and claim restored.\n"); return;
    }
    if (loop && result==RP1_GPIO_BUSY) {
        console_write("GPIO loopback requires isolated pins and the 3.3 V GPIO bank.\n"); return;
    }
    if (result==RP1_GPIO_SIGNAL) {
        console_write("GPIO mismatch step="); console_u64(sample.step+1);
        console_write(" pin="); console_u64(sample.pin);
        console_write(" expected="); console_u64(sample.expected);
        console_write(" pad="); console_u64(sample.pad);
        console_write(" rio="); console_u64(sample.rio);
        console_write(" status="); console_hex(sample.status); console_write("\n");
    }
    static const char *const report[]={
        [RP1_GPIO_OK]="PASS: GPIO RIO latch and enable verified, pad isolated, state and claim restored.\n",
        [RP1_GPIO_BUSY]="GPIO test refused: pin is not isolated.\n",
        [RP1_GPIO_IO]="GPIO test failed: register readback could not be verified.\n",
        [RP1_GPIO_RESTORE]="GPIO test failed: restore unverified, claims retained until reboot.\n",
        [RP1_GPIO_SIGNAL]="GPIO signal test failed. Pin state and claims restored.\n"
    };
    console_write(report[result]);
}

static void report_gpio(const void *dtb,uintptr_t uart,unsigned pin,enum gpio_command command)
{
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    const struct xhci_observer gpio={read_register,(void *)RP1_GPIO_BASE,RP1_GPIO_SPAN};
    if (uart!=RP1_LIVE_BASE+0x30000 || !mmu_dma_compatible() || !rp1_pci_description(dtb,0x200000) ||
        !rp1_gpio_description(dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_GPIO_OFFSET+RP1_GPIO_SPAN-1)!=RP1_SNAPSHOT_OK) {
        console_write("GPIO unavailable on this Pi 5 header profile.\n"); return;
    }
    if (command!=GPIO_READ) { test_gpio(&gpio,pin,command); return; }
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

void rp1_ethernet_command(uintptr_t dtb,uintptr_t uart,enum rp1_eth_action action)
{
#ifdef OUROS_PI5
    static unsigned ticket;
    static bool restore_failed;
    static struct rp1_eth_link link;
    if (action==ETH_DOWN && !ticket) { console_write("PHY hold released.\n"); return; }
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    if (restore_failed || uart!=RP1_LIVE_BASE+0x30000 || !rp1_pci_description((const void *)dtb,0x200000) ||
        !rp1_eth_description((const void *)dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_ETH_OFFSET+RP1_ETH_SPAN-1)!=RP1_SNAPSHOT_OK) {
        console_write("Ethernet probe unavailable.\n"); return;
    }
    bool needs_gpio=action==ETH_DOWN || (action!=ETH_PROBE && !link.held);
    if (needs_gpio && (!rp1_eth_reset_description((const void *)dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_GPIO_OFFSET+RP1_ETH_RESET_BANK+RP1_ETH_RESET_SPAN-1)!=RP1_SNAPSHOT_OK)) {
        console_write("Ethernet probe unavailable.\n"); return;
    }
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,RP1_LIVE_BASE+RP1_ETH_OFFSET,RP1_ETH_SPAN},
        {RESOURCE_GPIO,RP1_GPIO_BASE,RP1_ETH_RESET_GPIO,1}
    };
    if (!ticket) { ticket=resource_acquire("rp1-ethernet",claims,needs_gpio ? 2 : 1); }
    if (!ticket) { console_write("Ethernet probe unavailable.\n"); return; }
    const struct rp1_eth_io io={
        {read_register,(void *)(uintptr_t)claims[0].start,RP1_ETH_SPAN},
        {read_register,(void *)(RP1_GPIO_BASE+RP1_ETH_RESET_BANK),RP1_ETH_RESET_SPAN},
        write_register,ethernet_ticks,timer_frequency()
    };
    struct rp1_phy_snapshot snapshot={0};
    enum rp1_phy_result result=rp1_eth_link_control(&io,action,&link,&snapshot);
    if (result==RP1_PHY_RESTORE) { restore_failed=true; }
    else if (!link.held) { (void)resource_release(ticket); ticket=0; }
    if (action==ETH_DOWN && result==RP1_PHY_OK) { console_write("PHY hold released.\n"); return; }
    if (snapshot.pulsed) { console_write("PHY reset pulsed.\n"); }
    if (action!=ETH_DOWN) {
        console_write("GEM id="); console_hex(snapshot.mac_id);
        console_write(" phy=1 id="); console_hex(snapshot.phy_id);
    }
    if (result!=RP1_PHY_OK) {
        static const char *const errors[]={"", "invalid MAC", "MAC busy", "MDIO I/O", "no PHY reply", "restore unverified", "reset failed"};
        console_write(" ethprobe: "); console_write(errors[result]); console_write("\n"); return;
    }
    console_write(snapshot.status&4 ? " link=up" : " link=down");
    console_write(" autoneg=");
    console_write(!(snapshot.control&0x1000) ? "disabled" : snapshot.status&0x20 ? "complete" : "pending");
    console_write(link.held ? " held=yes" : " held=no");
    console_write("\n");
#else
    (void)dtb; (void)uart; (void)action;
    console_write("Ethernet probe requires a Raspberry Pi 5.\n");
#endif
}

bool rp1_gpio_command(const char *line,uintptr_t dtb,uintptr_t uart)
{
    const char *name="gpio";
    while (*name && *line==*name) { ++line; ++name; }
    if (*name) { return false; }
    enum gpio_command command;
    switch (*line) {
    case 'i': command=GPIO_READ; name="info"; break;
    case 't': command=GPIO_TEST; name="test"; break;
    case 'l': command=GPIO_LOOP; name="loop"; break;
    case 'p': command=GPIO_PULL; name="pull"; break;
    default: return false;
    }
    const char *suffix=name;
    while (*name && *line==*name) { ++line; ++name; }
    if (*name || (*line && *line!=' ')) { return false; }
    while (*line==' ') { ++line; }
    bool valid=*line>='0' && *line<='9';
    unsigned pin=0;
    while (*line>='0' && *line<='9' && pin<RP1_HEADER_GPIOS) {
        pin=pin*10+(unsigned)(*line++-'0');
    }
    while (*line==' ') { ++line; }
    if (*line || (command==GPIO_LOOP ? valid : !valid || pin>=RP1_HEADER_GPIOS)) {
        console_write("Usage: gpio"); console_write(suffix);
        console_write(command==GPIO_LOOP ? "\n" : " <BCM GPIO 0..27>\n");
        return true;
    }
#ifdef OUROS_PI5
    report_gpio((const void *)dtb,uart,command==GPIO_LOOP ? RP1_LOOP_TX : pin,command);
#else
    (void)dtb; (void)uart;
    console_write(command==GPIO_READ ? "GPIO readback requires a Raspberry Pi 5.\n" : "GPIO test requires a Raspberry Pi 5.\n");
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
