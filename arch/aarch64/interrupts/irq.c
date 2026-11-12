#include "irq.h"
#include "gic.h"
#include "tick.h"
#include "mmu.h"
#include "console.h"
#include "cpu.h"
static struct gic_state controller;
static struct tick_state tick;
static const char *board;
static uintptr_t dtb;
static bool running,failed;
static uint64_t unexpected,spurious,early;
static unsigned last_unexpected;
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
static bool initialize(void)
{
    (void)irq_save();
    if (controller.attempted) { console_write(controller.ready && !failed ? "IRQ controller already initialized.\n" : "IRQ initialization already attempted/failed; reboot before retry.\n");return controller.ready && !failed; }
    if (cpu_el()!=1 || !mmu_dma_compatible()) { console_write("IRQ init refused: requires EL1 and caches-off known memory profile.\n");return false; }
    struct gic_description description;
    if (!gic_discover(board,(const void *)dtb,0x200000,&description)) { console_write("IRQ init refused: unsupported DT GICv2/timer profile.\n");return false; }
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
        }else { ++unexpected;last_unexpected=id;(void)gic_mask_unexpected(&controller,id); }
        if (!good) { timer_control(0);running=false;failed=true; }
        __asm__ volatile("dsb sy" ::: "memory");if (gic_eoi(&controller,raw)!=GIC_OK) { failed=true; }
        if (failed) { break; }
    }
    irq_fatal_stop();failed=true;return false;
}
static void info(void)
{
    uint64_t flags=irq_save();struct tick_state t=tick;struct gic_state g=controller;bool active=running,error=failed;uint64_t bad=unexpected,empty=spurious,e=early,ctl=timer_status();irq_restore(flags);
    console_write("IRQ controller: ");console_write(!g.attempted ? "not initialized" : g.ready ? "GICv2 ready" : gic_error(g.result));console_write(" fault=");console_write(error ? "yes" : "no");
    console_write("\nPIDR2=");console_hex(g.pidr2);console_write(" TYPER=");console_hex(g.typer);console_write(" CPU_targets=");console_hex(g.cpu_targets);console_write(" PMR=");console_hex(g.priority_mask);
    console_write(" distributor=");console_hex(g.description.distributor);console_write(" cpu_interface=");console_hex(g.description.cpu);console_write("\nTimer: ");console_write(active ? "running" : "stopped");console_write(" CPU_IRQ=");console_write(flags&0x80 ? "masked" : "unmasked");
    console_write(" INTID=");console_u64(g.description.timer_irq);console_write(" CNTP_CTL=");console_hex(ctl);console_write("\nfrequency=");console_u64(t.frequency);console_write(" period_ticks=");console_u64(t.period);console_write(" delivered=");console_u64(t.delivered);console_write(" elapsed_periods=");console_u64(t.periods);console_write(" skipped=");console_u64(t.late);
    console_write("\nacknowledgements=");console_u64(g.acknowledgements);console_write(" completions=");console_u64(g.completions);console_write(" special/spurious=");console_u64(empty);console_write(" unexpected=");console_u64(bad);console_write(" last_unexpected=");console_u64(last_unexpected);console_write(" early=");console_u64(e);console_write("\nSingle CPU; timer ISR does not schedule tasks; no peripheral IRQ drivers yet.\n");
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
    if (same(line,"timertest")) { test();return true; }return false;
}
