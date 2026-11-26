#include "irq.h"
#include "gic.h"
#include "irq_probe.h"
#include "irq_work.h"
#include "tick.h"
#include "mmu.h"
#include "console.h"
#include "cpu.h"
#include "resources.h"
#ifdef OUROS_PI5
#include "rp1_probe.h"
#endif
static struct gic_state controller;
static struct tick_state tick;
static const char *board;
static uintptr_t dtb;
static bool running,failed;
static uint64_t unexpected,spurious,early;
static unsigned last_unexpected;
static struct irq_work peripheral;
static volatile bool deferred_ready;
static struct irq_probe probe;
static unsigned probe_events;
static unsigned sgi_ticket;
extern uint64_t irq_test_registers(uint64_t duration_ticks);
uint64_t irq_save(void)
{ uint64_t flags;__asm__ volatile("mrs %0, daif; msr daifset, #2; isb" : "=r"(flags) :: "memory");return flags; }
void irq_restore(uint64_t flags)
{ if (!(flags&0x80)) { __asm__ volatile("msr daifclr, #2; isb" ::: "memory"); }else { __asm__ volatile("msr daifset, #2; isb" ::: "memory"); } }
static void timer_control(uint64_t value) { __asm__ volatile("msr cntp_ctl_el0, %0; isb" :: "r"(value) : "memory"); }
static uint64_t timer_status(void) { uint64_t x;__asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(x));return x; }
static void deadline(uint64_t value) { __asm__ volatile("msr cntp_cval_el0, %0; isb" :: "r"(value) : "memory"); }
static bool read32(void *ctx,bool cpu,unsigned offset,uint32_t *out)
{
    (void)ctx;const struct gic_description *d=&controller.description;uint64_t base=cpu ? d->cpu : d->distributor;uint32_t span=cpu ? d->cpu_span : d->distributor_span;
    if (!out || (offset&3) || span<4 || offset>span-4) { return false; }
    __asm__ volatile("dmb sy" ::: "memory");*out=*(volatile uint32_t *)(uintptr_t)(base+offset);__asm__ volatile("dmb sy" ::: "memory");return true;
}
static bool write32(void *ctx,bool cpu,unsigned offset,uint32_t value)
{
    (void)ctx;const struct gic_description *d=&controller.description;uint64_t base=cpu ? d->cpu : d->distributor;uint32_t span=cpu ? d->cpu_span : d->distributor_span;
    if ((offset&3) || span<4 || offset>span-4) { return false; }
    __asm__ volatile("dsb sy" ::: "memory");*(volatile uint32_t *)(uintptr_t)(base+offset)=value;__asm__ volatile("dsb sy" ::: "memory");return true;
}
void irq_setup(const char *name,uintptr_t blob) { board=name;dtb=blob; }
void irq_fatal_stop(void) { __asm__ volatile("msr daifset, #2; isb" ::: "memory");if (running) { timer_control(0); }running=false; }
bool irq_timer_running(void) { uint64_t f=irq_save();bool value=running;irq_restore(f);return value; }
uint64_t irq_timer_delivered(void) { uint64_t f=irq_save(),value=tick.delivered;irq_restore(f);return value; }
struct irq_counters irq_read_counters(void)
{
    uint64_t saved=irq_save();
    struct irq_counters snapshot={tick.delivered,tick.late,unexpected,running};
    irq_restore(saved);
    return snapshot;
}
const struct gic_state *irq_claim_sgi(void)
{
    if (!controller.ready || failed || !gic_sgi_target(&controller)) { return NULL; }
    if (!sgi_ticket) {
        const struct resource_range claim={RESOURCE_IRQ,controller.description.distributor,GIC_WORK_SGI,1};
        sgi_ticket=resource_acquire("smp-ipi",&claim,1);
    }
    return sgi_ticket ? &controller : NULL;
}
bool irq_release_sgi(void)
{
    if (sgi_ticket && !resource_release(sgi_ticket)) { return false; }
    sgi_ticket=0;
    return true;
}
static bool initialize(void)
{
    (void)irq_save();
    if (controller.attempted) { console_write(controller.ready && !failed ? "IRQ controller already initialized.\n" : "IRQ initialization already attempted/failed; reboot before retry.\n");return controller.ready && !failed; }
    if (cpu_el()!=1 || !mmu_dma_compatible()) { console_write("IRQ init refused: requires EL1 and caches-off known memory profile.\n");return false; }
    struct gic_description description;
    if (!gic_discover(board,(const void *)dtb,0x200000,&description)) { console_write("IRQ init refused: unsupported DT GICv2/timer profile.\n");return false; }
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,description.distributor,description.distributor_span},
        {RESOURCE_MMIO,0,description.cpu,description.cpu_span},
        {RESOURCE_IRQ,description.distributor,description.timer_irq,1}
    };
    if (!resource_acquire("gic-timer",claims,3)) {
        console_write("IRQ init refused: resource claim unavailable.\n");return false;
    }
    timer_control(0);controller.description=description;
    const struct gic_io io={NULL,read32,write32};enum gic_result r=gic_initialize(&controller,&description,&io);
    console_write("GICv2 initialization: ");console_write(gic_error(r));console_write(" timer_INTID=");console_u64(description.timer_irq);console_write(". CPU IRQ stays masked until timerstart.\n");return r==GIC_OK;
}
static bool start(void)
{
    (void)irq_save();
    if (running) { console_write("Timer already running.\n");__asm__ volatile("msr daifclr, #2; isb" ::: "memory");return true; }
    if (!controller.ready || failed) { console_write("Timer start refused: run irqinit successfully first; reboot after an IRQ fault.\n");return false; }
    if (!tick_init(timer_frequency(),100,timer_ticks(),&tick)) { console_write("Timer start refused: unsupported counter frequency/deadline.\n");return false; }
    timer_control(0);deadline(tick.deadline);
    if (gic_timer_enable(&controller)!=GIC_OK) { failed=true;console_write("Timer PPI enable failed; IRQs remain masked.\n");return false; }
    running=true;timer_control(1);__asm__ volatile("msr daifclr, #2; isb" ::: "memory");
    console_write("Periodic physical timer started: requested_hz=100; CPU IRQ unmasked.\n");return true;
}
bool irq_ensure_periodic(void)
{
    return irq_timer_running() || (initialize() && start());
}
static void stop(void)
{
    (void)irq_save();timer_control(0);running=false;
    if (controller.attempted && gic_timer_disable(&controller)!=GIC_OK) { failed=true; }
}
bool irq_dispatch(void)
{
    if (!controller.ready || failed) { irq_fatal_stop();failed=true;return false; }
    for (unsigned n=0;n<16;++n) {
        uint32_t raw;if (gic_ack(&controller,&raw)!=GIC_OK) { break; }unsigned id=raw&1023;
        if (id>=1020) { ++spurious;return !failed; }
        bool good=false;
        if (id==controller.description.timer_irq && running) {
            /* Stop the level source before EOIR; late service skips elapsed
             * deadlines once rather than generating an interrupt burst. */
            timer_control(2);enum tick_result r=tick_advance(&tick,timer_ticks());
            if (r==TICK_OK || r==TICK_EARLY) { if (r==TICK_EARLY) { ++early; }deadline(tick.deadline);timer_control(1);good=true; }
        } else {
            enum irq_work_result result=irq_work_receive(&peripheral,id);
            if (result==IRQ_WORK_QUEUED) {
                deferred_ready=true;good=true;
            } else if (result==IRQ_WORK_MASK) {
                good=gic_mask_line(&controller,id)==GIC_OK;
            } else {
                ++unexpected;last_unexpected=id;
                good=gic_mask_unexpected(&controller,id)==GIC_OK;
            }
        }
        if (!good) { timer_control(0);running=false;failed=true; }
        __asm__ volatile("dsb sy" ::: "memory");if (gic_eoi(&controller,raw)!=GIC_OK) { failed=true; }
        if (failed) { break; }
    }
    irq_fatal_stop();failed=true;return false;
}
void irq_run_deferred(void)
{
    if (!deferred_ready) { return; }
    void *context=0;
    uint64_t flags=irq_save();
    if (flags&0x80) { return; }
    irq_deferred_fn fn=irq_work_take(&peripheral,&context);
    if (!fn) { deferred_ready=false; }
    irq_restore(flags);
    if (fn) { fn(context); }
}
static void probe_done(void *context)
{
    (void)context;
    ++probe_events;
}
static bool ack_edge(void *context)
{ (void)context; return true; } /* IAR consumes the synthetic edge. */

static unsigned rp1_test_intid(void)
{
#ifdef OUROS_PI5
    struct rp1_topology topology;
    struct rp1_mip mip;
    if (!rp1_topology_read((const void *)dtb,0x200000,&topology) ||
        !rp1_mip_describe((const void *)dtb,0x200000,topology.msi_parent,&mip) ||
        mip.vectors!=64 || mip.data_offset) { return 0; }
    return mip.first_intid+63; /* The endpoint has sources 0..60. */
#else
    return 223; /* Same unused test INTID on the supported QEMU virt profile. */
#endif
}

static void peripheral_test(bool edge)
{
    if (edge) {
        probe.intid=rp1_test_intid();
    } else if (!irq_probe_discover(board,(const void *)dtb,0x200000,&probe)) {
        probe.intid=0;
    }
    if (!probe.intid) { console_write("Peripheral IRQ test unavailable for this device tree.\n");return; }
    uint64_t flags=irq_save();
    if (!running || failed || (flags&0x80)) {
        irq_restore(flags);console_write("Peripheral IRQ test requires a running timer.\n");return;
    }
    if (!edge && !irq_probe_available(&probe)) {
        irq_restore(flags);console_write("Peripheral IRQ test refused: source busy.\n");return;
    }
    const struct resource_range claims[]={
        {RESOURCE_IRQ,controller.description.distributor,probe.intid,1},
        {RESOURCE_MMIO,0,probe.base,0x1000}
    };
    unsigned ticket=resource_acquire("irq-test",claims,edge ? 1 : 2);
    if (!ticket) {
        irq_restore(flags);console_write("Peripheral IRQ test refused: resource claim unavailable.\n");return;
    }
    struct gic_spi_route route;
    enum gic_result status=edge ? gic_spi_claim_edge(&controller,probe.intid,&route) :
                                 gic_spi_claim(&controller,probe.intid,&route);
    if (status!=GIC_OK) {
        if (!controller.ready) { irq_fatal_stop();failed=true; }
        else { (void)resource_release(ticket);irq_restore(flags); }
        console_write("Peripheral SPI claim refused: ");console_write(gic_error(status));console_write("\n");return;
    }
    if (!irq_work_bind(&peripheral,probe.intid,edge ? ack_edge : irq_probe_ack,probe_done,&probe)) {
        if (gic_spi_release(&controller,&route)!=GIC_OK) { irq_fatal_stop();failed=true; }
        else { (void)resource_release(ticket);irq_restore(flags); }
        console_write("Peripheral IRQ test refused: registry unavailable.\n");return;
    }
    uint64_t before=tick.delivered,received=peripheral.received;
    probe_events=0;
    status=gic_spi_enable(&controller,&route);
    if (status==GIC_OK) {
        if (edge) { status=gic_spi_test_pending(&controller,&route); }
        else { irq_probe_arm(&probe); }
    }
    irq_restore(flags);
    if (status==GIC_OK) {
        uint64_t start=timer_ticks(),limit=timer_frequency()*4;
        while (timer_ticks()-start<limit) {
            irq_run_deferred();
            if (probe_events && irq_timer_delivered()!=before) { break; }
        }
    }
    (void)irq_save();
    if (!edge && status==GIC_OK) { irq_probe_restore(&probe); }
    bool clean=gic_spi_release(&controller,&route)==GIC_OK;
    if (!clean) { irq_fatal_stop();failed=true; }
    if (!failed) { irq_restore(flags); }
    for (unsigned i=0;i<IRQ_WORK_PENDING_MAX;++i) { irq_run_deferred(); }
    (void)irq_save();
    if (!irq_work_unbind(&peripheral,probe.intid)) { clean=false; }
    uint64_t ticks=tick.delivered-before,handled=peripheral.received-received;
    clean=clean && !failed;
    if (clean) { clean=resource_release(ticket); }
    if (!failed) { irq_restore(flags); }

    console_write("Peripheral IRQ source=");
    console_write(edge ? "GIC software edge" : probe.kind==IRQ_PROBE_BCM_TIMER ? "BCM system timer compare 3" : "PL031 alarm");
    console_write(" intid=");console_u64(probe.intid);console_write(" received=");console_u64(handled);
    console_write(" deferred=");console_u64(probe_events);console_write(" timer_delta=");console_u64(ticks);console_write("\n");
    console_write(status==GIC_OK && clean && handled==1 && probe_events==1 && ticks ?
        "PASS: peripheral IRQ acknowledged, deferred outside IRQ, timer still running.\n" :
        "FAIL: peripheral IRQ test could not complete.\n");
}
static void info(void)
{
    uint64_t flags=irq_save();struct tick_state t=tick;struct gic_state g=controller;bool active=running,error=failed;uint64_t bad=unexpected,empty=spurious,e=early,ctl=timer_status();irq_restore(flags);
    console_write("IRQ controller: ");console_write(!g.attempted ? "not initialized" : g.ready ? "GICv2 ready" : gic_error(g.result));console_write(" fault=");console_write(error ? "yes" : "no");
    console_write("\nPIDR2=");console_hex(g.pidr2);console_write(" TYPER=");console_hex(g.typer);console_write(" CPU_targets=");console_hex(g.cpu_targets);console_write(" PMR=");console_hex(g.priority_mask);
    console_write(" distributor=");console_hex(g.description.distributor);console_write(" cpu_interface=");console_hex(g.description.cpu);console_write("\nTimer: ");console_write(active ? "running" : "stopped");console_write(" CPU_IRQ=");console_write(flags&0x80 ? "masked" : "unmasked");
    console_write(" INTID=");console_u64(g.description.timer_irq);console_write(" CNTP_CTL=");console_hex(ctl);console_write("\nfrequency=");console_u64(t.frequency);console_write(" period_ticks=");console_u64(t.period);console_write(" delivered=");console_u64(t.delivered);console_write(" elapsed_periods=");console_u64(t.periods);console_write(" skipped=");console_u64(t.late);
    console_write("\nacknowledgements=");console_u64(g.acknowledgements);console_write(" completions=");console_u64(g.completions);console_write(" special/spurious=");console_u64(empty);console_write(" unexpected=");console_u64(bad);console_write(" last_unexpected=");console_u64(last_unexpected);console_write(" early=");console_u64(e);
    flags=irq_save();
    uint64_t received=peripheral.received,serviced=peripheral.serviced,overflow=peripheral.overflow,stuck=peripheral.stuck;
    irq_restore(flags);
    console_write("\nperipheral_received=");console_u64(received);console_write(" deferred=");console_u64(serviced);
    console_write(" overflow=");console_u64(overflow);console_write(" stuck=");console_u64(stuck);console_write("\n");
}
static void test(void)
{
    if (!irq_timer_running()) { console_write("Timer test unavailable: run irqinit then timerstart.\n");return; }
    uint64_t flags=irq_save(),before=tick.delivered;irq_restore(flags);uint64_t hz=timer_frequency();uint64_t registers=irq_test_registers(hz/10);uint64_t begin=timer_ticks();unsigned loops=0;
    while (timer_ticks()-begin<hz/2 && ++loops<10000000) { __asm__ volatile("nop"); }
    flags=irq_save();uint64_t after=tick.delivered;bool good=!registers && running && !failed && after-before>=5 && timer_ticks()-begin>=hz/4;irq_restore(flags);
    console_write(good ? "PASS: periodic timer IRQs delivered; asynchronous GPR/SP/NZCV sentinels preserved and foreground responsive.\n" : "FAIL: timer IRQ delivery/register return/deadline.\n");
}
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
bool irq_command(const char *line)
{
    if (same(line,"irqinit")) { if (irq_timer_running()) { console_write("Timer is running; timerstop before irqinit.\n"); }else { (void)initialize(); }return true; }
    if (same(line,"timerstart")) { (void)start();return true; }
    if (same(line,"timerstop")) { stop();console_write("Periodic timer stopped; CPU IRQ masked.\n");return true; }
    if (same(line,"irqinfo") || same(line,"timerinfo")) { info();return true; }
    if (same(line,"irqtest")) { peripheral_test(false);return true; }
    if (same(line,"rp1irqtest")) { peripheral_test(true);return true; }
    if (same(line,"timertest")) { test();return true; }return false;
}
