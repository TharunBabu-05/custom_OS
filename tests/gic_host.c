#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_assert.h"
#include "gic.h"
#include "dt_property.h"
#include "irq_probe.h"
#include "irq_work.h"
static struct gic_state state;
static struct { uint32_t d[1024],c[1024];unsigned calls,fail_at,writes;bool inaccessible,bad_pmr,disable_ignored;uint32_t eoi; } model;
static bool read32(void *ctx,bool cpu,unsigned at,uint32_t *v)
{
    assert(ctx==&model && !(at&3) && at<0x1000);if (++model.calls==model.fail_at) { return false; }
    *v=(cpu ? model.c : model.d)[at/4];return true;
}
static bool write32(void *ctx,bool cpu,unsigned at,uint32_t value)
{
    assert(ctx==&model && !(at&3) && at<0x1000);if (++model.calls==model.fail_at) { return false; }++model.writes;
    if (cpu) {
        assert(at==0 || at==4 || at==16);
        if (at==16) { model.eoi=value;return true; }
        if (at==4 && model.bad_pmr) { return true; }model.c[at/4]=value;return true;
    }
    assert(at==0 || (at>=0x100 && at<0x300) ||
        at==0x40c || at==0x41c || at==0x428 || at==0x828 || at==0xc04 || at==0xc08 ||
        at==0xf00 || at==0xf1c);
    if (at>=0x100 && at<0x180) { if (!model.inaccessible) { model.d[at/4]|=value; }return true; }
    if (at>=0x180 && at<0x200) { if (!model.disable_ignored) { model.d[(at-0x80)/4]&=~value; }return true; }
    if (at>=0x200 && at<0x280) { model.d[at/4]|=value;return true; }
    if (at>=0x280 && at<0x300) { model.d[(at-0x80)/4]&=~value;return true; }
    if (at==0xf1c) {
        model.d[at/4]&=~value;
        if (!(model.d[at/4]>>24)) { model.d[0x200/4]&=~(1u<<GIC_WORK_SGI); }
        return true;
    }
    if (at==0x41c && model.inaccessible) { return true; }model.d[at/4]=value;return true;
}
static const struct gic_io io={&model,read32,write32};
static const struct gic_description description={0x08000000,0x08010000,0x10000,0x10000,30};
static void seed(void)
{
    memset(&state,0,sizeof(state));memset(&model,0,sizeof(model));
    model.d[0xfe8/4]=0x20;model.d[4/4]=15|(3<<5)|(1<<10);model.d[0x800/4]=0x01010101;
    model.d[0x100/4]=0x12345678|(1u<<30);model.d[0x200/4]=0x12345678|(1u<<30);model.d[0x41c/4]=0x12345678;model.d[0xc04/4]=0xffffffff;model.c[0]=0x260;
    model.d[0x80/4]=0x55aa55aa;model.d[0x104/4]=0x100;model.d[0xc08/4]=0xaabbccdd;
}
static void driver(void)
{
    seed();assert(gic_initialize(&state,&description,&io)==GIC_OK && state.ready && state.attempted && !state.timer_enabled);
    unsigned calls=model.calls;assert(model.d[0x80/4]==0x55aa55aa && model.d[0x104/4]==0x100 && model.d[0xc08/4]==0xaabbccdd);
    assert(model.d[0x41c/4]==0x12a05678 && model.d[0xc04/4]==0xdfffffff && model.c[0]==0x61 && model.c[1]==0xf0);
    assert(!(model.d[0x100/4]&(1u<<30)) && !(model.d[0x200/4]&(1u<<30)));
    assert(gic_initialize(&state,&description,&io)==GIC_STATE && model.calls==calls);
    assert(gic_timer_enable(&state)==GIC_OK && state.timer_enabled && (model.d[0x100/4]&(1u<<30)));
    model.c[12/4]=0x1c1e;uint32_t raw=0;assert(gic_ack(&state,&raw)==GIC_OK && raw==0x1c1e && state.last_id==30 && gic_eoi(&state,raw)==GIC_OK && model.eoi==raw && state.completions==1);
    raw=0x1407;assert(gic_eoi(&state,raw)==GIC_OK && model.eoi==raw); /* SGI token source bits preserved. */
    assert(gic_timer_disable(&state)==GIC_OK && !state.timer_enabled && !(model.d[0x100/4]&(1u<<30)));
    assert(gic_mask_unexpected(&state,40)==GIC_OK && !model.d[0x104/4] && state.masked_unexpected==1);
    calls=model.calls;assert(gic_mask_unexpected(&state,7)==GIC_UNSUPPORTED && gic_mask_unexpected(&state,1020)==GIC_ARGUMENT && gic_eoi(&state,1023)==GIC_STATE && model.calls==calls);
    seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);model.c[12/4]=1023;assert(gic_ack(&state,&raw)==GIC_OK && raw==1023);calls=model.calls;assert(gic_eoi(&state,raw)==GIC_STATE && model.calls==calls);
    seed();model.d[0xfe8/4]=0x2b;model.d[4/4]=8;model.d[0x800/4]=0;assert(gic_initialize(&state,&description,&io)==GIC_OK && state.ready && !state.cpu_targets && !(model.d[0x100/4]&(1u<<30)));
    assert(gic_timer_enable(&state)==GIC_OK && gic_timer_disable(&state)==GIC_OK); /* QEMU UP ITARGETSR RAZ/WI. */
    puts("PASS: GICv2 PPI-only masked init, priority/level RMW preserves neighbors/security/SPI state, nonsecure combined EOI mode, timer enable/disable, raw IAR/EOIR token preservation, spurious/SGI masking refusal and unexpected SPI masking");
}
static void failures(void)
{
    seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);unsigned steps=model.calls;
    for (unsigned fail=1;fail<=steps;++fail) { seed();model.fail_at=fail;assert(gic_initialize(&state,&description,&io)==GIC_IO && !state.ready && state.attempted);if (fail<=8) { assert(!model.writes); }model.fail_at=0;assert(gic_timer_disable(&state)==GIC_OK); }
    for (unsigned mode=0;mode<7;++mode) {
        seed();if (!mode) { model.d[0xfe8/4]=0x30; }if (mode==1) { model.d[0x800/4]=0; }if (mode==2) { model.d[0x800/4]=0x03030303; }if (mode==3) { model.d[0x300/4]=1u<<30; }
        if (mode==4) { model.inaccessible=true; }if (mode==5) { model.bad_pmr=true; }if (mode==6) { model.disable_ignored=true; }
        enum gic_result result=gic_initialize(&state,&description,&io);assert(result==(mode<4 ? GIC_UNSUPPORTED : GIC_STATE) && !state.ready);
        if (mode<4) { assert(!model.writes); }
    }
    for (unsigned mode=0;mode<5;++mode) {
        seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);model.fail_at=model.calls+1;
        uint32_t raw;enum gic_result r=mode==0 ? gic_timer_enable(&state) : mode==1 ? gic_timer_disable(&state) : mode==2 ? gic_ack(&state,&raw) : mode==3 ? gic_eoi(&state,30) : gic_mask_unexpected(&state,40);
        assert(r==GIC_IO && !state.ready);
    }
    struct gic_description bad=description;bad.timer_irq=29;seed();assert(gic_initialize(&state,&bad,&io)==GIC_ARGUMENT && !model.calls && !state.attempted);
    puts("PASS: every GIC init I/O failure and operation failures stop readiness, timer cleanup remains possible, unsupported architecture/targets/active timer and inaccessible NS priority/enable/PMR readback refused before IRQ unmask");
}
static void spi_routes(void)
{
    seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);
    struct gic_spi_route route;
    unsigned before=model.writes;
    assert(gic_spi_claim(&state,30,&route)==GIC_ARGUMENT && model.writes==before);
    assert(gic_spi_claim(&state,40,&route)==GIC_STATE && model.writes==before); /* Already enabled */
    model.d[0x104/4]=0x200;
    model.d[0x204/4]=0x100;
    assert(gic_spi_claim(&state,40,&route)==GIC_STATE && model.writes==before);
    model.d[0x204/4]=0;
    model.d[0x304/4]=0x100;
    assert(gic_spi_claim(&state,40,&route)==GIC_STATE && model.writes==before);
    model.d[0x304/4]=0;
    model.d[0xc08/4]=0x20000; /* Edge-select bit for ID 40 */
    assert(gic_spi_claim(&state,40,&route)==GIC_STATE && model.writes==before);
    model.d[0xc08/4]=0;
    model.d[0x428/4]=0x72463250;
    model.d[0x828/4]=0x01020408;
    unsigned first=model.calls;
    assert(gic_spi_claim(&state,40,&route)==GIC_OK && route.priority==0x50 && route.target==8);
    unsigned steps=model.calls-first;
    assert(model.d[0x428/4]==0x724632a0 && model.d[0x828/4]==0x01020401 && model.d[0x104/4]==0x200);
    assert(gic_spi_enable(&state,&route)==GIC_OK && model.d[0x104/4]==0x300);
    uint32_t raw;model.c[12/4]=40;
    assert(gic_ack(&state,&raw)==GIC_OK && raw==40);
    assert(gic_mask_line(&state,40)==GIC_OK && model.d[0x104/4]==0x200 && !state.masked_unexpected);
    assert(gic_eoi(&state,raw)==GIC_OK && model.eoi==40);
    assert(gic_spi_release(&state,&route)==GIC_OK && model.d[0x428/4]==0x72463250 && model.d[0x828/4]==0x01020408);
    for (unsigned step=1;step<=steps;++step) {
        seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);
        model.d[0x104/4]=0x200;
        model.d[0xc08/4]=0;
        model.d[0x428/4]=0x72463250;model.d[0x828/4]=0x01020408;
        model.fail_at=model.calls+step;
        assert(gic_spi_claim(&state,40,&route)==GIC_IO && state.ready);
        assert(model.d[0x428/4]==0x72463250 && model.d[0x828/4]==0x01020408 && model.d[0x104/4]==0x200);
    }
    model.c[12/4]=41;model.fail_at=0;
    assert(gic_ack(&state,&raw)==GIC_OK && raw==41);
    model.disable_ignored=true;
    assert(gic_mask_unexpected(&state,41)==GIC_STATE && !state.ready);
    assert(gic_eoi(&state,raw)==GIC_OK && model.eoi==raw);
    seed();model.d[4/4]=8;model.d[0x800/4]=0;
    model.d[0x104/4]=0x200;model.d[0xc08/4]=0;model.d[0x428/4]=0x72463250;
    assert(gic_initialize(&state,&description,&io)==GIC_OK && !state.cpu_targets);
    before=model.writes;
    assert(gic_spi_claim(&state,40,&route)==GIC_OK && model.writes==before+1 && !model.d[0x828/4]);
    assert(gic_spi_enable(&state,&route)==GIC_OK && gic_spi_release(&state,&route)==GIC_OK);
    assert(model.d[0x428/4]==0x72463250 && !model.d[0x828/4] && model.d[0x104/4]==0x200);
    model.d[0xc08/4]=0x80000000u;
    assert(gic_spi_claim_edge(&state,40,&route)==GIC_OK && route.restore_trigger && !route.edge);
    assert(model.d[0xc08/4]==0x80020000u);
    assert(gic_spi_enable(&state,&route)==GIC_OK && gic_spi_test_pending(&state,&route)==GIC_OK);
    assert(model.d[0x204/4]==0x100 && model.d[0x104/4]==0x300);
    assert(gic_spi_release(&state,&route)==GIC_OK && model.d[0xc08/4]==0x80000000u &&
           model.d[0x204/4]==0 && model.d[0x104/4]==0x200);
    model.d[0xc08/4]|=0x20000u;
    assert(gic_spi_claim_edge(&state,40,&route)==GIC_OK && route.edge);
    assert(gic_spi_release(&state,&route)==GIC_OK && model.d[0xc08/4]==0x80020000u);
    model.d[0x204/4]=0x100;
    before=model.writes;
    assert(gic_spi_claim_edge(&state,40,&route)==GIC_STATE && model.writes==before);
    puts("PASS: edge SPI software pending, neighboring trigger modes, and restoring level and edge routes.");
    puts("PASS: SPI ownership, programmable and fixed routing, adjacent fields, rollback, quarantine and raw EOI on mask failure.");
}
static void seed_sgi(void)
{
    seed();assert(gic_initialize(&state,&description,&io)==GIC_OK);
    model.d[0x800/4]=0x04040404;
    model.d[0x100/4]=0x3456;
    model.d[0x40c/4]=0xc824282c;
    model.d[0xf1c/4]=0x00020301;
    model.c[0]=0x260;
    model.c[1]=0xe0;
}
static void sgi_restored(uint32_t enable)
{
    assert(model.d[0x100/4]==enable && model.d[0x40c/4]==0xc824282c && model.d[0xf1c/4]==0x00020301);
    assert(model.c[0]==0x260 && model.c[1]==0xe0 && state.ready && state.cpu_targets==0x01010101);
    assert(!state.timer_enabled && !state.acknowledgements && !state.completions);
}
static void sgi_routes(void)
{
    seed_sgi();
    struct gic_sgi_route route={0};
    uint32_t bit=1u<<GIC_WORK_SGI;
    assert(gic_sgi_target(&state)==4);
    unsigned before=model.calls;
    assert(gic_sgi_open(&state,&route) && route.target==4 && route.saved);
    unsigned steps=model.calls-before;
    assert(model.d[0x40c/4]==0x4024282c && model.d[0x100/4]==(0x3456|bit) &&
           model.c[0]==0x61 && model.c[1]==0x80);
    before=model.writes;
    assert(!gic_sgi_open(&state,&route) && !gic_sgi_send(&state,0) &&
           !gic_sgi_send(&state,0x10) && model.writes==before);
    assert(gic_sgi_send(&state,0x06) && model.d[0xf00/4]==0x0006000f);
    uint32_t raw;
    model.c[12/4]=0x80f;
    assert(gic_sgi_take(&state,&raw) && raw==0x80f && model.eoi==raw);
    model.c[12/4]=1023;
    assert(gic_sgi_take(&state,&raw) && raw==1023 && model.eoi==0x80f);
    model.d[0xf1c/4]|=0x03000000;
    model.d[0x200/4]|=bit;
    assert(gic_sgi_close(&state,&route) && !route.saved && !route.target && !(model.d[0x200/4]&bit));
    sgi_restored(0x3456);

    seed_sgi();model.d[0x100/4]|=bit;
    assert(gic_sgi_open(&state,&route) && gic_sgi_close(&state,&route));
    sgi_restored(0x3456|bit);
    for (unsigned step=1;step<=steps;++step) {
        seed_sgi();model.fail_at=model.calls+step;
        assert(!gic_sgi_open(&state,&route) && !route.saved);
        sgi_restored(0x3456);
    }
    seed_sgi();
    assert(gic_sgi_open(&state,&route));
    model.fail_at=model.calls+1;
    assert(!gic_sgi_close(&state,&route) && route.saved && !route.target);
    assert(gic_sgi_close(&state,&route) && !route.saved);
    sgi_restored(0x3456);

    before=model.writes;
    model.d[0x800/4]=0x05050505;assert(!gic_sgi_target(&state) && !gic_sgi_open(&state,&route));
    model.d[0x800/4]=0x10101010;assert(!gic_sgi_target(&state) && !gic_sgi_open(&state,&route));
    model.d[0x800/4]=0x04040404;
    model.d[0x200/4]|=bit;assert(!gic_sgi_open(&state,&route));
    model.d[0x200/4]&=~bit;model.d[0x300/4]|=bit;assert(!gic_sgi_open(&state,&route));
    assert(model.writes==before);
    puts("PASS: banked SGI target checks, setup rollback, pending cleanup, restore retry and full IAR tokens.");
}
struct event_probe { unsigned acks,calls;bool stuck; };
static bool event_ack(void *context)
{ struct event_probe *probe=context;++probe->acks;return !probe->stuck; }
static void event_deferred(void *context)
{ struct event_probe *probe=context;++probe->calls; }
static void deferred_work(void)
{
    struct irq_work work={0};
    struct event_probe normal={0},broken={.stuck=true},other={0};
    assert(!irq_work_bind(&work,30,event_ack,event_deferred,&normal));
    assert(irq_work_bind(&work,34,event_ack,event_deferred,&normal));
    assert(!irq_work_bind(&work,34,event_ack,event_deferred,&normal));
    assert(irq_work_bind(&work,35,event_ack,event_deferred,&broken));
    assert(irq_work_bind(&work,36,event_ack,event_deferred,&other));
    assert(irq_work_bind(&work,37,event_ack,event_deferred,&normal));
    assert(!irq_work_bind(&work,38,event_ack,event_deferred,&normal));
    assert(irq_work_receive(&work,38)==IRQ_WORK_UNBOUND);
    assert(irq_work_receive(&work,35)==IRQ_WORK_MASK && irq_work_receive(&work,35)==IRQ_WORK_MASK && broken.acks==1);
    for (unsigned i=0;i<IRQ_WORK_PENDING_MAX;++i) { assert(irq_work_receive(&work,34)==IRQ_WORK_QUEUED); }
    assert(irq_work_receive(&work,34)==IRQ_WORK_MASK && irq_work_receive(&work,34)==IRQ_WORK_MASK);
    assert(normal.acks==IRQ_WORK_PENDING_MAX+1 && !normal.calls && !irq_work_unbind(&work,34));
    for (unsigned i=0;i<IRQ_WORK_PENDING_MAX;++i) {
        void *context=0;irq_deferred_fn fn=irq_work_take(&work,&context);
        assert(fn==event_deferred && context==&normal);fn(context);
    }
    void *unused=0;assert(!irq_work_take(&work,&unused));
    assert(work.overflow==1 && work.stuck==1 && work.serviced==IRQ_WORK_PENDING_MAX);
    assert(irq_work_unbind(&work,34) && irq_work_unbind(&work,35));
    assert(irq_work_bind(&work,34,event_ack,event_deferred,&normal) && irq_work_receive(&work,34)==IRQ_WORK_QUEUED);
    assert(irq_work_receive(&work,34)==IRQ_WORK_QUEUED && irq_work_receive(&work,36)==IRQ_WORK_QUEUED);
    assert(irq_work_take(&work,&unused)==event_deferred && unused==&other);
    assert(irq_work_take(&work,&unused)==event_deferred && unused==&normal);
    assert(irq_work_receive(&work,36)==IRQ_WORK_QUEUED);
    assert(irq_work_take(&work,&unused)==event_deferred && unused==&other);
    assert(irq_work_take(&work,&unused)==event_deferred && unused==&normal);
    puts("PASS: bounded IRQ registrations, round-robin dispatch, unowned and stuck sources, overflow quarantine and safe reuse.");
}
static unsigned char *load(const char *path,size_t *length)
{
    FILE *file=fopen(path,"rb");assert(file && !fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0 && size<0x200000 && !fseek(file,0,SEEK_SET));unsigned char *data=malloc((size_t)size);assert(data && fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);*length=(size_t)size;return data;
}
static void dt(const char *path,bool pi)
{
    size_t n;unsigned char *data=load(path,&n);struct gic_description d;const char *board=pi ? "rpi5-header" : "qemu_virt";
    assert(gic_discover(board,data,n,&d) && d.timer_irq==30 && d.distributor==(pi ? 0x107fff9000ULL : 0x08000000) && d.cpu==(pi ? 0x107fffa000ULL : 0x08010000));
    struct irq_probe probe;
    assert(irq_probe_discover(board,data,n,&probe) && probe.intid==(pi ? 99 : 34) &&
        probe.base==(pi ? 0x107c003000ULL : 0x09010000));
    const unsigned char *irq;uint32_t irq_bytes;
    assert(dt_property(data,n,pi ? "/soc@107c000000/timer@7c003000" : "/pl031@9010000","interrupts",&irq,&irq_bytes));
    size_t irq_flags=(size_t)(irq-data)+irq_bytes-1;
    data[irq_flags]^=1;assert(!irq_probe_discover(board,data,n,&probe));data[irq_flags]^=1;
    struct gic_description sentinel;memset(&sentinel,0xa5,sizeof(sentinel));
    for (size_t bytes=0;bytes<n;++bytes) { d=sentinel;assert(!gic_discover(board,data,bytes,&d) && !memcmp(&d,&sentinel,sizeof(d))); }
    const unsigned char *p;uint32_t bytes;assert(dt_property(data,n,"/timer","interrupts",&p,&bytes) && (bytes==48 || (!pi && bytes==60)));size_t offset=(size_t)(p-data);unsigned char old=data[offset+19];data[offset+19]=13;d=sentinel;assert(!gic_discover(board,data,n,&d) && !memcmp(&d,&sentinel,sizeof(d)));data[offset+19]=old;
    if (bytes==60) {
        old=data[offset+55];data[offset+55]^=1;d=sentinel;
        assert(!gic_discover(board,data,n,&d) && !memcmp(&d,&sentinel,sizeof(d)));data[offset+55]=old;
        data[offset+58]^=2;d=sentinel;
        assert(!gic_discover(board,data,n,&d) && !memcmp(&d,&sentinel,sizeof(d)));data[offset+58]^=2;
    }
    if (!pi) {
        assert(dt_property(data,n,"/intc@8000000","reg",&p,&bytes));
        if (bytes==64) { size_t extra=(size_t)(p-data)+39;old=data[extra];data[extra]^=1;d=sentinel;assert(!gic_discover(board,data,n,&d) && !memcmp(&d,&sentinel,sizeof(d)));data[extra]=old; }
    }
    uint32_t random=17;unsigned char *mutated=malloc(n);assert(mutated);
    for (unsigned i=0;i<2000;++i) { memcpy(mutated,data,n);random=random*1664525u+1013904223u;mutated[random%n]^=(unsigned char)(1u<<(i%8));d=sentinel;if (gic_discover(board,mutated,n,&d)) { assert(d.distributor==(pi ? 0x107fff9000ULL : 0x08000000) && d.cpu==(pi ? 0x107fffa000ULL : 0x08010000) && d.timer_irq==30); }else { assert(!memcmp(&d,&sentinel,sizeof(d))); } }
    free(mutated);free(data);printf("PASS: %s GIC/timer DT binding/translation/CPU mask discovery, every truncation, altered timer rejection, 2000 mutations and unchanged output on failure\n",board);
}
int main(int argc,char **argv)
{
    assert(argc==5);
    driver();failures();spi_routes();sgi_routes();deferred_work();
    dt(argv[1],true);
    for (int i=2;i<argc;++i) { dt(argv[i],false); }
    return 0;
}
