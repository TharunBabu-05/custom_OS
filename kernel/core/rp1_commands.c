#include "console.h"
#include "rp1_probe.h"
#include "rp1_msix.h"

enum gpio_command { GPIO_READ, GPIO_TEST, GPIO_LOOP, GPIO_PULL };

#ifdef OUROS_PI5
#include "cpu.h"
#include "crypto.h"
#include "irq.h"
#include "memory.h"
#include "mmu.h"
#include "net_wire.h"
#include "network_session.h"
#include "network_worker.h"
#include "process.h"
#include "resources.h"
#include "rng200.h"
#include "rp1_eth_loop.h"
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

static void ethernet_barrier(void *context)
{
    (void)context;
    __asm__ volatile("dsb sy" ::: "memory");
}

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
struct eth_dma_page { uint64_t physical,bus; unsigned ticket; bool background; };
enum wired_state { WIRED_DOWN,WIRED_UP,WIRED_WAITING,WIRED_FAILED,WIRED_DHCP };
static struct wired_network {
    struct eth_dma_page page;
    struct rp1_eth_dma session;
    struct rp1_eth_dma_stats stats;
    struct dhcp_client dhcp;
    struct netprobe claim;
    uintptr_t dtb;
    unsigned speed,losses,recoveries,speed_changes;
    enum rp1_eth_loop_result result;
    enum rp1_phy_result phy_result;
    enum wired_state state;
    enum block_result dhcp_result;
    bool tcp,random_failed,dhcp_mode,dhcp_apply,dhcp_cancelled,dhcp_link_lost,lease_expired,decline_sent,lease_started;
} wired;
static uint64_t dhcp_retry_after;

static const struct rp1_dma_io eth_dma_io={
    (void *)(RP1_LIVE_BASE+RP1_ETH_OFFSET),read_register,write_register,ethernet_ticks,ethernet_barrier
};

static bool free_ethernet_page(uint64_t physical,bool background)
{
    return background ? network_worker_dma_free(physical) : page_free(physical);
}

static bool ethernet_dma_page(const struct xhci_observer *bridge,const void *tree,bool held,bool background,struct eth_dma_page *page)
{
    _Static_assert(sizeof(struct rp1_eth_loop_memory)<=PAGE_SIZE,"GEM DMA scratch");
    _Static_assert(sizeof(struct rp1_eth_dma_memory)<=PAGE_SIZE,"GEM network DMA");
    struct pi5_dma_window window;
    uint64_t physical,bus;
    if (!mmu_dma_compatible() || !pi5_dma_description(tree,0x200000) ||
        !pi5_dma_window_read(bridge,&window) ||
        !(background ? network_worker_dma_alloc(&physical) : page_alloc(&physical))) {
        console_write("Ethernet DMA unavailable.\n"); return false;
    }
    const uintptr_t base=RP1_LIVE_BASE+RP1_ETH_OFFSET;
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,base,RP1_ETH_SPAN}, {RESOURCE_DMA_MEMORY,0,physical,PAGE_SIZE}
    };
    if (!mmu_dma_buffer(physical,PAGE_SIZE) || !pi5_dma_address(&window,physical,PAGE_SIZE,&bus) ||
        bus>UINT32_MAX-(PAGE_SIZE-1)) {
        (void)free_ethernet_page(physical,background);
        console_write("Ethernet DMA unavailable.\n"); return false;
    }
    /* Cable tests run under the existing PHY hold's GEM claim. */
    unsigned ticket=resource_acquire("ethernet-dma",claims+held,2-held);
    if (!ticket) {
        (void)free_ethernet_page(physical,background);
        console_write("Ethernet DMA claim refused.\n"); return false;
    }
    *page=(struct eth_dma_page){physical,bus,ticket,background};
    return true;
}

static enum rp1_eth_loop_result release_ethernet_dma(const struct eth_dma_page *page,enum rp1_eth_loop_result result)
{
    if (result!=GEM_LOOP_RETAIN &&
        (!resource_release(page->ticket) || !free_ethernet_page(page->physical,page->background))) { return GEM_LOOP_RETAIN; }
    return result;
}

static struct ethernet_ipv4 wired_ipv4;

static const struct ethernet_ipv4 *ethernet_config(void)
{
    return wired_ipv4.prefix ? &wired_ipv4 : &rp1_eth_default_ipv4;
}

static void print_ipv4(const uint8_t *address)
{
    for (unsigned i=0;i<4;++i) {
        if (i) { console_write("."); }
        console_u64(address[i]);
    }
}

static void print_ethernet_address(void)
{
    const struct ethernet_ipv4 *ipv4=wired.state==WIRED_UP && wired.dhcp_apply ? &wired.session.ipv4 : ethernet_config();
    print_ipv4(ipv4->address);
    console_write("/");
    console_u64(ipv4->prefix);
}

static bool set_ethernet_address(const char *arg)
{
    char literal[16];
    unsigned count=0;
    while (*arg && *arg!='/' && count<sizeof(literal)-1) { literal[count++]=*arg++; }
    if (*arg!='/') { return false; }
    literal[count]=0;
    ++arg;
    unsigned prefix=0;
    while (*arg>='0' && *arg<='9' && prefix<=30) { prefix=prefix*10+(unsigned)(*arg++-'0'); }
    while (*arg==' ') { ++arg; }
    uint8_t address[4];
    return !*arg && network_parse_ipv4(literal,address) &&
           ethernet_ipv4_configure(&wired_ipv4,address,prefix);
}

static bool wired_sockets(const struct ethernet_ipv4 *ipv4,const unsigned char secret[32])
{
    if (!udp_link_up(rp1_eth_mac,ipv4->address,ipv4->mask)) { return false; }
    if (wired.tcp && !tcp_link_up(rp1_eth_mac,ipv4->address,ipv4->mask,secret)) {
        udp_link_down();return false;
    }
    wired.state=WIRED_UP;
    return true;
}

static bool wired_open(struct rp1_eth_dma *session,unsigned speed)
{
    unsigned char secret[32];
    wired.random_failed=wired.tcp && (!platform_rng_prepare(wired.dtb) ||
                                     platform_random_bytes(secret,sizeof(secret))!=BLOCK_OK);
    struct rp1_eth_dma_memory *memory=(void *)(uintptr_t)wired.page.physical;
    const struct ethernet_ipv4 *ipv4=ethernet_config();
    wired.result=wired.random_failed ? GEM_LOOP_SETUP :
                 rp1_eth_dma_open(session,&eth_dma_io,memory,(uint32_t)wired.page.bus,speed,ipv4,wired.tcp,&wired.stats);
    bool opened=wired.result==GEM_LOOP_OK && wired_sockets(ipv4,secret);
    crypto_wipe(secret,sizeof(secret));
    if (wired.result!=GEM_LOOP_OK) { return false; }
    if (!opened) {
        wired.result=rp1_eth_dma_close(session) ? GEM_LOOP_BUSY : GEM_LOOP_RETAIN;
        return false;
    }
    wired.speed=speed;
    return true;
}

static void wired_close(struct rp1_eth_dma *session)
{
    if (wired.tcp) { tcp_link_down(); }
    udp_link_down();
    if (!rp1_eth_dma_close(session)) { wired.result=GEM_LOOP_RETAIN; }
}

static void wired_worker(void *unused)
{
    (void)unused;
    struct rp1_eth_dma *session=&wired.session;
    const struct rp1_eth_io phy={
        .gem={read_register,eth_dma_io.context,RP1_ETH_SPAN},
        .write=write_register,.ticks=ethernet_ticks,.hz=timer_frequency()
    };
    const uint64_t interval=phy.hz/5;
    uint64_t polled=timer_ticks();
    unsigned candidate=0;
    if (wired_open(session,wired.speed)) {
        while (network_worker_pause()) {
            uint64_t now=timer_ticks();
            bool online=wired.state==WIRED_UP;
            enum block_result transfer=BLOCK_OK;
            if (now-polled<interval) {
                if (!online) { continue; }
                uint64_t ms=now/phy.hz*1000+now%phy.hz*1000/phy.hz;
                transfer=rp1_eth_dma_service(session,ms);
                if (transfer==BLOCK_OK) { continue; }
            }
            polled=now;
            struct rp1_phy_snapshot snapshot;
            wired.phy_result=rp1_phy_sample(&phy,online,&snapshot);
            if (wired.phy_result!=RP1_PHY_OK) { break; }
            unsigned speed=rp1_phy_full_duplex_speed(&snapshot);
            if (online) {
                if (speed!=wired.speed) {
                    ++wired.losses;
                    wired_close(session);
                    wired.state=WIRED_WAITING;
                    if (wired.result!=GEM_LOOP_OK) { break; }
                    candidate=speed;
                } else if (transfer!=BLOCK_OK) {
                    wired.result=GEM_LOOP_FRAME;
                    break;
                }
            } else if (speed && speed==candidate) {
                unsigned previous=wired.speed;
                if (!wired_open(session,speed)) { break; }
                ++wired.recoveries;
                if (previous!=speed) { ++wired.speed_changes; }
                candidate=0;
            } else {
                candidate=speed;
            }
        }
    }
    if (wired.state==WIRED_UP) { wired_close(session); }
    wired.result=release_ethernet_dma(&wired.page,wired.result);
    wired.state=wired.result==GEM_LOOP_OK && wired.phy_result==RP1_PHY_OK ? WIRED_DOWN : WIRED_FAILED;
}

struct wired_dhcp_context { struct rp1_eth_io phy; uint64_t polled,lease_origin; };

static uint64_t wired_dhcp_ms(void *context)
{
    const struct wired_dhcp_context *d=context;
    uint64_t now=timer_ticks(),hz=d->phy.hz;
    return now/hz*1000+now%hz*1000/hz;
}

static bool wired_dhcp_ready(void *context)
{
    (void)context;
    return wired.state==WIRED_DHCP;
}

static enum block_result wired_dhcp_check(struct wired_dhcp_context *d)
{
    if (wired.dhcp_link_lost) { return BLOCK_NOT_READY; }
    uint64_t now=timer_ticks();
    if (now-d->polled>=d->phy.hz/5) {
        d->polled=now;
        struct rp1_phy_snapshot phy;
        wired.phy_result=rp1_phy_sample(&d->phy,true,&phy);
        if (wired.phy_result!=RP1_PHY_OK) { return BLOCK_IO; }
        if (rp1_phy_full_duplex_speed(&phy)!=wired.speed) {
            wired.dhcp_link_lost=true;
            return BLOCK_NOT_READY;
        }
    }
    return BLOCK_OK;
}

static enum block_result wired_dhcp_poll(void *context)
{
    enum block_result result=wired_dhcp_check(context);
    return result==BLOCK_OK ? rp1_eth_dma_dhcp_poll(&wired.session,&wired.dhcp,wired_dhcp_ms(context)) : result;
}

static bool wired_dhcp_pause(void)
{
    if (network_worker_pause()) { return true; }
    wired.dhcp_cancelled=true;
    return false;
}

static enum block_result wired_dhcp_delay(void *context,unsigned us)
{
    const struct wired_dhcp_context *d=context;
    uint64_t ticks=(d->phy.hz*us+999999)/1000000,start=timer_ticks();
    while (timer_ticks()-start<ticks) {
        if (!wired_dhcp_pause()) { return BLOCK_NOT_READY; }
    }
    return BLOCK_OK;
}

static enum block_result wired_dhcp_complete(void *context,enum block_result result)
{
    while (result==BLOCK_OK && wired.session.pending) {
        if (!wired_dhcp_pause()) { return BLOCK_NOT_READY; }
        result=wired_dhcp_poll(context);
    }
    return result;
}

static enum block_result wired_dhcp_tx(void *context,const void *frame,size_t bytes)
{
    struct wired_dhcp_context *d=context;
    uint64_t now=wired_dhcp_ms(context);
    if (wired.dhcp.phase==DHCP_REQUESTING && !wired.dhcp.request_sent) { d->lease_origin=now; }
    return wired_dhcp_complete(context,rp1_eth_dma_dhcp_send(&wired.session,frame,bytes,now));
}

static enum block_result wired_claim_tx(void *context,enum netprobe_packet kind,unsigned sequence,const void *frame,size_t bytes)
{
    if (sequence) { return BLOCK_ARGUMENT; }
    return wired_dhcp_complete(context,rp1_eth_dma_claim_send(&wired.session,kind,frame,bytes,wired_dhcp_ms(context)));
}

static bool wired_lease_expired(struct wired_dhcp_context *d)
{
    wired.lease_expired=wired_dhcp_ms(d)-d->lease_origin>=(uint64_t)wired.dhcp.lease.seconds*1000;
    return wired.lease_expired;
}

static enum block_result wired_claim(struct wired_dhcp_context *d,const unsigned char acd_entropy[32],const unsigned char secret[32])
{
    const struct dhcp_lease *lease=&wired.dhcp.lease;
    struct ethernet_ipv4 ipv4;
    uint32_t mask=net_be32(lease->mask);
    if (!lease->have_mask || mask==UINT32_MAX ||
        !ethernet_ipv4_configure(&ipv4,lease->address,(unsigned)__builtin_clz(~mask)) || net_be32(ipv4.mask)!=mask) { return BLOCK_UNSUPPORTED; }
    const struct netprobe_io io={d,wired_claim_tx,wired_dhcp_poll,wired_dhcp_delay,wired_dhcp_ms,wired_dhcp_ready};
    wired.session.claim=&wired.claim;
    enum block_result result=netprobe_claim(&io,rp1_eth_mac,lease,acd_entropy,d->lease_origin,&wired.claim);
    if (wired.claim.conflict) {
        unsigned char frame[DHCP_FRAME_BYTES];size_t bytes;
        if (!wired.dhcp_cancelled && !wired_lease_expired(d) &&
            dhcp_encode_decline(&wired.dhcp,frame,sizeof(frame),&bytes)==BLOCK_OK) {
            wired.decline_sent=wired_dhcp_tx(d,frame,bytes)==BLOCK_OK;
        }
        return BLOCK_IO;
    }
    if (wired_lease_expired(d)) { return BLOCK_TIMEOUT; }
    if (result!=BLOCK_OK) { return result; }
    wired.session.ipv4=ipv4;
    wired.session.tcp_enabled=true;
    if (!wired_sockets(&ipv4,secret)) { return BLOCK_IO; }
    wired.lease_started=true;
    while (wired_dhcp_pause()) {
        if (wired_lease_expired(d)) { return BLOCK_TIMEOUT; }
        result=wired_dhcp_check(d);
        if (result!=BLOCK_OK) { return result; }
        result=rp1_eth_dma_service(&wired.session,wired_dhcp_ms(d));
        if (result!=BLOCK_OK) { return result; }
    }
    return BLOCK_OK;
}

static void wired_dhcp_worker(void *unused)
{
    (void)unused;
    unsigned char entropy[74];
    wired.random_failed=!platform_rng_prepare(wired.dtb) || platform_random_bytes(entropy,wired.dhcp_apply ? sizeof(entropy) : 10)!=BLOCK_OK;
    struct rp1_eth_dma_memory *memory=(void *)(uintptr_t)wired.page.physical;
    wired.result=wired.random_failed ? GEM_LOOP_SETUP :
        rp1_eth_dma_open(&wired.session,&eth_dma_io,memory,(uint32_t)wired.page.bus,wired.speed,NULL,false,&wired.stats);
    wired.dhcp_result=BLOCK_NOT_READY;
    if (wired.result==GEM_LOOP_OK) {
        struct wired_dhcp_context context={
            .phy={{read_register,eth_dma_io.context,RP1_ETH_SPAN},{0},write_register,ethernet_ticks,timer_frequency()},
            .polled=timer_ticks()
        };
        const struct dhcp_io io={&context,wired_dhcp_tx,wired_dhcp_poll,wired_dhcp_delay,wired_dhcp_ms,wired_dhcp_ready};
        wired.state=WIRED_DHCP;
        wired.dhcp_result=dhcp_probe_run(&io,rp1_eth_mac,entropy,&wired.dhcp);
        if (wired.dhcp_result==BLOCK_OK && wired.dhcp_apply) {
            wired.dhcp_result=wired_claim(&context,entropy+10,entropy+42);
        }
        if (wired.claim.conflict) { dhcp_retry_after=timer_ticks()+10*context.phy.hz; }
        netprobe_close(&wired.claim);
        if (wired.state==WIRED_UP) { wired_close(&wired.session); }
        else if (!rp1_eth_dma_close(&wired.session)) { wired.result=GEM_LOOP_RETAIN; }
    }
    crypto_wipe(entropy,sizeof(entropy));
    wired.result=release_ethernet_dma(&wired.page,wired.result);
    wired.state=wired.result==GEM_LOOP_OK && wired.phy_result==RP1_PHY_OK ? WIRED_DOWN : WIRED_FAILED;
}

static void ethernet_worker_up(const struct xhci_observer *bridge,const void *tree,unsigned speed,enum rp1_eth_action action)
{
    if (network_worker_active()) { console_write("Stop the active network worker first.\n"); return; }
    bool dhcp=action==ETH_DHCP || action==ETH_DHCP_UP;
    if (dhcp && timer_ticks()<dhcp_retry_after) { console_write("Wait ten seconds after a DHCP address conflict.\n");return; }
    struct eth_dma_page page;
    if (!ethernet_dma_page(bridge,tree,true,true,&page)) { return; }
    wired=(struct wired_network){.page=page,.dtb=(uintptr_t)tree,.speed=speed,.tcp=action==ETH_TCP_UP || action==ETH_DHCP_UP,
        .dhcp_mode=dhcp,.dhcp_apply=action==ETH_DHCP_UP,.dhcp_result=BLOCK_NOT_READY};
    if (!network_worker_start(wired.dhcp_mode ? wired_dhcp_worker : wired_worker,NULL)) {
        wired.result=release_ethernet_dma(&wired.page,GEM_LOOP_BUSY);
        wired.state=WIRED_FAILED;
    }
    if (wired.state==WIRED_UP) {
        console_write(wired.tcp ? "Ethernet TCP and UDP" : "Ethernet UDP");
        console_write(" active at ");
        print_ethernet_address();
        console_write(". Use ethdown to stop.\n");
    } else if (wired.state==WIRED_DHCP) {
        console_write(wired.dhcp_apply ? "Ethernet DHCP activation started. Use ethlease.\n" :
                      "Ethernet DHCP probe started. Use ethlease.\n");
    } else if (wired.random_failed) {
        console_write("Ethernet session needs hardware randomness. Use rngtest for status.\n");
    } else {
        console_write("Ethernet network result="); console_u64(wired.result); console_write("\n");
    }
}

static bool ethernet_exchange(const struct xhci_observer *bridge,const void *tree,unsigned speed,bool ping)
{
    struct eth_dma_page page;
    if (!ethernet_dma_page(bridge,tree,speed!=0,false,&page)) { return true; }
    struct rp1_eth_loop_report report;
    volatile struct rp1_eth_loop_memory *memory=(void *)(uintptr_t)page.physical;
    uint64_t timeout=timer_frequency()/(speed ? 1 : 4);
    enum rp1_eth_loop_result result=ping ? rp1_eth_ping(&eth_dma_io,memory,page.bus,timeout,speed,&report) :
                                          rp1_eth_exchange(&eth_dma_io,memory,page.bus,timeout,speed,&report);
    result=release_ethernet_dma(&page,result);
    if (result==GEM_LOOP_OK) {
        if (speed) {
            console_write("PASS: "); console_write(ping ? "ICMP echo" : "ARP");
            console_write(" reply from 192.0.2.1 mac="); console_hex(report.peer_mac); console_write("\n");
        } else {
            console_write("PASS: GEM looped back 96 bytes, guards intact, DMA page and claims released.\n");
        }
        return true;
    }
    if (ping) { console_write(report.echo ? "ICMP " : "ARP "); }
    console_write("Ethernet DMA result="); console_u64(result);
    console_write(" queues="); console_hex(report.queues);
    console_write(" imr="); console_hex(report.mask);
    console_write(" tx="); console_hex(report.tx);
    console_write(" rx="); console_hex(report.rx);
    console_write(" tsr="); console_hex(report.tsr);
    console_write(" rsr="); console_hex(report.rsr);
    if (report.register_failed) {
        console_write(" reg="); console_hex(report.offset);
        console_write(" expected="); console_hex(report.expected);
        console_write(" observed="); console_hex(report.observed);
    }
    console_write(result==GEM_LOOP_RETAIN ? " DMA page retained. Reboot before retrying.\n" : "\n");
    return result!=GEM_LOOP_RETAIN;
}
#endif

bool rp1_ethernet_active(void)
{
#ifdef OUROS_PI5
    return wired.state==WIRED_UP || wired.state==WIRED_WAITING || wired.state==WIRED_DHCP;
#else
    return false;
#endif
}

void rp1_ethernet_status(void)
{
#ifdef OUROS_PI5
    static const char *const states[]={"down","up","waiting","failed","dhcp"};
    console_write("Ethernet state=");console_write(states[wired.state]);
    console_write(" Mbps=");console_u64(wired.state==WIRED_UP || wired.state==WIRED_DHCP ? wired.speed : 0);
    console_write(" losses=");console_u64(wired.losses);
    console_write(" recoveries=");console_u64(wired.recoveries);
    console_write(" speed_changes=");console_u64(wired.speed_changes);
    console_write(" dma=");console_u64(wired.result);
    console_write(" phy=");console_u64(wired.phy_result);
    console_write("\n");
#else
    console_write("Ethernet status requires a Raspberry Pi 5.\n");
#endif
}

void rp1_ethernet_stats(void)
{
#ifdef OUROS_PI5
    if (wired.state==WIRED_UP || wired.state==WIRED_DHCP) { rp1_eth_dma_sample(&wired.session); }
    const struct rp1_eth_dma_stats *s=&wired.stats;
    console_write("Ethernet tx_submitted=");console_u64(s->tx_submitted);
    console_write(" tx_completed=");console_u64(s->tx_completed);
    console_write(" tx_timeouts=");console_u64(s->tx_timeouts);
    console_write("\nEthernet rx_frames=");console_u64(s->rx_frames);
    console_write(" rx_discarded=");console_u64(s->rx_discarded);
    console_write(" rx_overruns=");console_u64(s->rx_overruns);
    console_write("\nEthernet mac_tx=");console_u64(s->mac_tx);
    console_write(" mac_rx=");console_u64(s->mac_rx);
    console_write(wired.session.io && !s->mac_unavailable ? " mac_stats=ok\n" : " mac_stats=unavailable\n");
#else
    console_write("Ethernet statistics require a Raspberry Pi 5.\n");
#endif
}

void rp1_ethernet_lease(void)
{
#ifdef OUROS_PI5
    if (!wired.dhcp_mode) { console_write("No wired DHCP probe. Use ethdhcp.\n"); return; }
    const struct dhcp_client *d=&wired.dhcp;
    const char *state="failed";
    if (wired.state==WIRED_UP) { state="bound"; }
    else if (wired.state==WIRED_DHCP) {
        state=d->phase==DHCP_ACKED ? "checking-address" : d->phase==DHCP_REQUESTING ? "requesting" : "selecting";
    }
    else if (wired.result==GEM_LOOP_RETAIN) { state="dma-retained"; }
    else if (wired.claim.conflict) { state="conflict"; }
    else if (wired.lease_expired) { state="expired"; }
    else if (wired.dhcp_cancelled) { state=wired.lease_started ? "stopped" : "cancelled"; }
    else if (wired.dhcp_link_lost) { state="link-lost"; }
    else if (d->nak_seen) { state="nak"; }
    else if (wired.dhcp_result==BLOCK_TIMEOUT) { state="timeout"; }
    else if (d->phase==DHCP_ACKED && !wired.dhcp_apply) { state="ack"; }
    console_write("Ethernet DHCP state=");console_write(state);
    console_write(" result=");console_u64(wired.dhcp_result);
    console_write("\nDHCP discover=");console_u64(d->discover_sent);
    console_write(" request=");console_u64(d->request_sent);
    console_write(" rx=");console_u64(d->received);
    console_write(" ignored=");console_u64(d->ignored);
    console_write(" malformed=");console_u64(d->malformed);
    console_write(" other=");console_u64(wired.session.dhcp_other);
    console_write(" polls=");console_u64(d->polls);
    console_write("\n");
    if (wired.dhcp_result==BLOCK_TIMEOUT && !d->received) {
        console_write("No DHCP replies received. Check the DHCP server on the attached LAN.\n");
    }
    if (d->have_offer) {
        bool ack=d->phase==DHCP_ACKED;
        const struct dhcp_lease *l=ack ? &d->lease : &d->offer;
        console_write(ack ? "ACK address=" : "Offer address="); print_ipv4(l->address);
        console_write(" server="); print_ipv4(l->server);
        console_write(" lease_seconds="); console_u64(l->seconds);
        if (l->have_mask) { console_write(" mask="); print_ipv4(l->mask); }
        if (l->have_router) { console_write(" router="); print_ipv4(l->router); }
        if (l->have_dns) { console_write(" dns="); print_ipv4(l->dns); }
        if (l->t1_seconds) { console_write(" renewal_seconds="); console_u64(l->t1_seconds); }
        if (l->t2_seconds) { console_write(" rebind_seconds="); console_u64(l->t2_seconds); }
        console_write(wired.state==WIRED_UP ? "\nLease active=yes\n" : "\nLease active=no\n");
    }
    if (wired.dhcp_apply && d->phase==DHCP_ACKED) {
        console_write("ARP probes=");console_u64(wired.claim.probes);
        console_write(" announcements=");console_u64(wired.claim.announcements);
        if (wired.claim.conflict) {
            console_write(" conflict_mac=");
            console_hex((uint64_t)net_be16(wired.claim.conflict_mac)<<32|net_be32(wired.claim.conflict_mac+2));
            console_write(wired.decline_sent ? " decline=sent" : " decline=not-sent");
        }
        console_write("\n");
    }
#else
    console_write("Ethernet DHCP requires a Raspberry Pi 5.\n");
#endif
}

void rp1_ethernet_dhcp_cancel(void)
{
#ifdef OUROS_PI5
    if (wired.state!=WIRED_DHCP) { console_write("No active wired DHCP probe.\n"); return; }
    if (!network_worker_stop()) { console_write("Ethernet DHCP worker busy.\n"); return; }
    rp1_ethernet_lease();
#else
    console_write("Ethernet DHCP requires a Raspberry Pi 5.\n");
#endif
}

void rp1_ethernet_dhcp_wait(void)
{
#ifdef OUROS_PI5
    if (wired.state==WIRED_DHCP) {
        uint64_t start=timer_ticks(),limit=timer_frequency()*80;
        console_write("Waiting for DHCP. Ctrl+C returns to the monitor.\n");
        while (wired.state==WIRED_DHCP && irq_timer_running() && timer_ticks()-start<limit) {
            if (console_getc()==3) { break; }
            irq_run_deferred();
            network_worker_service();
            process_service_step();
            if (wired.state==WIRED_DHCP && irq_timer_running()) { __asm__ volatile("wfi" ::: "memory"); }
        }
    }
    rp1_ethernet_lease();
#else
    console_write("Ethernet DHCP requires a Raspberry Pi 5.\n");
#endif
}

bool rp1_ethernet_address_command(const char *line)
{
    for (const char *name="ethaddr";*name;++name) {
        if (*line++!=*name) { return false; }
    }
    if (*line && *line!=' ') { return false; }
#ifdef OUROS_PI5
    while (*line==' ') { ++line; }
    if (line[0]=='i' && line[1]=='p' && !line[2]) {
        if (wired.state==WIRED_UP) { print_ipv4(wired.session.ipv4.address);console_write("\n"); }
        else { console_write("No active Ethernet IPv4 address. Use ethlease for DHCP status.\n"); }
    } else if (*line && rp1_ethernet_active()) {
        console_write("Run ethdown before changing the Ethernet address.\n");
    } else if (*line && !set_ethernet_address(line)) {
        console_write("Usage: ethaddr [ip|IPv4/prefix] with a usable host address and prefix 1..30.\n");
    } else {
        console_write("Ethernet IPv4 ");
        print_ethernet_address();
        console_write(wired.state!=WIRED_UP ? " active=no\n" : wired.dhcp_apply ? " active=yes source=dhcp\n" : " active=yes source=static\n");
    }
#else
    console_write("Ethernet addressing requires a Raspberry Pi 5.\n");
#endif
    return true;
}

void rp1_ethernet_command(uintptr_t dtb,uintptr_t uart,enum rp1_eth_action action)
{
#ifdef OUROS_PI5
    static unsigned ticket;
    static bool restore_failed;
    static struct rp1_eth_link link;
    if (rp1_ethernet_active() && (action!=ETH_DOWN || !network_worker_stop())) {
        console_write("Ethernet active. Use ethdown.\n"); return;
    }
    if (wired.result==GEM_LOOP_RETAIN) { console_write("Ethernet DMA retained until reboot.\n"); return; }
    if (wired.phy_result==RP1_PHY_RESTORE) { console_write("Ethernet PHY register restore unverified. Reboot before retrying.\n"); return; }
    if (action==ETH_DOWN && !ticket) { console_write("PHY hold released.\n"); return; }
    const struct xhci_observer bridge={read_register,(void *)RP1_PCIE_BASE,0x9310};
    if (restore_failed || uart!=RP1_LIVE_BASE+0x30000 || !rp1_pci_description((const void *)dtb,0x200000) ||
        !rp1_eth_description((const void *)dtb,0x200000) ||
        rp1_reachable(&bridge,RP1_ETH_OFFSET+RP1_ETH_SPAN-1)!=RP1_SNAPSHOT_OK) {
        console_write("Ethernet probe unavailable.\n"); return;
    }
    if (action==ETH_LOOP) {
        if (link.held) { console_write("Run ethdown before ethloop.\n"); return; }
        (void)ethernet_exchange(&bridge,(const void *)dtb,0,false);
        return;
    }
    bool worker=action==ETH_UDP_UP || action==ETH_TCP_UP || action==ETH_DHCP || action==ETH_DHCP_UP;
    bool wire=action==ETH_ARP || action==ETH_PING || worker;
    if ((wire || action==ETH_RECOVER) && !link.held) {
        console_write("Run ethup first.\n"); return;
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
    if (wire && result==RP1_PHY_OK) {
        unsigned speed=rp1_phy_full_duplex_speed(&snapshot);
        if (!speed) {
            console_write(snapshot.status&4 ? "Ethernet needs full duplex. status=" : "Ethernet link down. status=");
            console_hex(snapshot.status);
            console_write(" aux="); console_hex(snapshot.aux); console_write("\n"); return;
        }
        console_write("Ethernet link Mbps="); console_u64(speed); console_write("\n");
        if (worker) { ethernet_worker_up(&bridge,(const void *)dtb,speed,action); }
        else { restore_failed=!ethernet_exchange(&bridge,(const void *)dtb,speed,action==ETH_PING); }
        return;
    }
    if (action==ETH_DOWN && result==RP1_PHY_OK) { console_write("PHY hold released.\n"); return; }
    if (snapshot.pulsed) { console_write("PHY reset pulsed.\n"); }
    if (snapshot.restarted) { console_write("PHY restart requested.\n"); }
    if (action!=ETH_DOWN) {
        console_write("GEM id="); console_hex(snapshot.mac_id);
        console_write(" phy=1 id="); console_hex(snapshot.phy_id);
    }
    if (result!=RP1_PHY_OK) {
        static const char *const errors[]={"", "invalid MAC", "MAC busy", "MDIO I/O", "no PHY reply", "restore unverified", "reset failed", "PHY mode"};
        console_write(" ethprobe: "); console_write(errors[result]); console_write("\n"); return;
    }
    console_write(snapshot.status&4 ? " link=up" : " link=down");
    console_write(" autoneg=");
    console_write(!(snapshot.control&0x1000) ? "disabled" : snapshot.status&0x20 ? "complete" : "pending");
    console_write(link.held ? " held=yes" : " held=no");
    console_write("\n");
#else
    (void)dtb; (void)uart; (void)action;
    if (action==ETH_DHCP || action==ETH_DHCP_UP) { console_write("Ethernet DHCP requires a Raspberry Pi 5.\n"); return; }
    console_write(action==ETH_LOOP || action==ETH_ARP || action==ETH_PING || action==ETH_UDP_UP || action==ETH_TCP_UP ?
                  "Ethernet DMA test requires a Raspberry Pi 5.\n" :
                  "Ethernet probe requires a Raspberry Pi 5.\n");
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
