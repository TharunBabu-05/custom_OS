/* Original narrow GICv2 non-secure interface driver. Only PPI30 is configured;
 * no global SPI defaults, security-group writes or interrupt source guessing.
 * References: Arm IHI0048B GICv2; NS EOImodeNS is GICC_CTLR bit 9. */
#include "gic.h"
enum { D_CTLR=0,D_TYPER=4,D_ENABLE=0x100,D_DISABLE=0x180,D_PENDING_CLEAR=0x280,D_ACTIVE=0x300,
       D_PRIORITY=0x400,D_TARGET=0x800,D_CONFIG=0xc00,D_PIDR2=0xfe8,
       C_CTLR=0,C_PMR=4,C_IAR=12,C_EOI=16 };
static enum gic_result fail(struct gic_state *s,enum gic_result r)
{ s->ready=false;s->result=r;return r; }
static bool read(struct gic_state *s,bool cpu,unsigned at,uint32_t *v)
{ return s->io.read32(s->io.context,cpu,at,v); }
static bool write(struct gic_state *s,bool cpu,unsigned at,uint32_t v)
{ s->changed=true;return s->io.write32(s->io.context,cpu,at,v); }
static bool description(const struct gic_description *d)
{ return d && d->distributor && d->cpu && !(d->distributor&0xfff) && !(d->cpu&0xfff) && d->distributor_span>=0x1000 && d->cpu_span>=0x1000 && d->timer_irq==30; }
enum gic_result gic_initialize(struct gic_state *s,const struct gic_description *d,const struct gic_io *io)
{
    if (!s || !description(d) || !io || !io->read32 || !io->write32) { return GIC_ARGUMENT; }
    if (s->attempted) { return GIC_STATE; }
    *s=(struct gic_state){.description=*d,.io=*io,.attempted=true,.result=GIC_STATE};
    uint32_t active,priority,configuration;unsigned shift=(d->timer_irq&3)*8,edge=1u<<((d->timer_irq&15)*2+1);uint32_t bit=1u<<d->timer_irq;
    if (!read(s,false,D_PIDR2,&s->pidr2) || !read(s,false,D_TYPER,&s->typer) || !read(s,false,D_TARGET,&s->cpu_targets) ||
        !read(s,false,D_CTLR,&s->distributor_ctlr) || !read(s,true,C_CTLR,&s->cpu_ctlr) || !read(s,false,D_ACTIVE,&active) ||
        !read(s,false,D_PRIORITY+4*(d->timer_irq/4),&priority) || !read(s,false,D_CONFIG+4,&configuration)) { return fail(s,GIC_IO); }
    unsigned target=(s->cpu_targets|(s->cpu_targets>>8)|(s->cpu_targets>>16)|(s->cpu_targets>>24))&255;
    unsigned cpu_count=((s->typer>>5)&7)+1;
    /* A uniprocessor GIC can expose ITARGETSR as RAZ/WI (QEMU v9.2 does).
     * Its banked PPI needs no target routing. On multicore interfaces require
     * one accessible one-hot CPU target; never invent an SPI destination. */
    bool target_valid=target ? !(target&(target-1)) && target<(1u<<cpu_count) : cpu_count==1;
    if (((s->pidr2>>4)&15)!=2 || !target_valid || (active&bit)) { return fail(s,GIC_UNSUPPORTED); }
    /* EL1 runs non-secure. Secure IGROUPR is RAZ/WI here; firmware must have
     * exposed the NS physical timer as a Group1 PPI. Accessible priority/
     * enable readbacks below are checked; delivery is tested by timer IRQs. */
    if (!write(s,false,D_DISABLE,bit) || !write(s,false,D_PENDING_CLEAR,bit) ||
        !write(s,false,D_PRIORITY+4*(d->timer_irq/4),(priority&~(255u<<shift))|(0xa0u<<shift)) ||
        !write(s,false,D_CONFIG+4,configuration&~edge) ||
        !write(s,false,D_CTLR,s->distributor_ctlr|1) || !write(s,true,C_PMR,0xf0) ||
        !write(s,true,C_CTLR,(s->cpu_ctlr&~(1u<<9))|1)) { return fail(s,GIC_IO); }
    uint32_t enabled,p,cfg,dctl,cctl;
    if (!read(s,false,D_ENABLE,&enabled) || !read(s,false,D_PRIORITY+4*(d->timer_irq/4),&p) || !read(s,false,D_CONFIG+4,&cfg) ||
        !read(s,false,D_CTLR,&dctl) || !read(s,true,C_CTLR,&cctl) || !read(s,true,C_PMR,&s->priority_mask)) { return fail(s,GIC_IO); }
    if ((enabled&bit) || ((p>>shift)&0xf0)!=0xa0 || (cfg&edge) || !(dctl&1) || !(cctl&1) || (cctl&(1u<<9)) || (s->priority_mask&0xf0)!=0xf0) { return fail(s,GIC_STATE); }
    s->distributor_ctlr=dctl;s->cpu_ctlr=cctl;s->ready=true;s->result=GIC_OK;return GIC_OK;
}
enum gic_result gic_timer_enable(struct gic_state *s)
{
    if (!s || !s->ready) { return GIC_STATE; }uint32_t bit=1u<<s->description.timer_irq,enabled;
    if (!write(s,false,D_ENABLE,bit) || !read(s,false,D_ENABLE,&enabled)) { return fail(s,GIC_IO); }
    if (!(enabled&bit)) { return fail(s,GIC_STATE); }s->timer_enabled=true;return GIC_OK;
}
enum gic_result gic_timer_disable(struct gic_state *s)
{
    if (!s || !s->attempted || !s->io.write32 || !s->io.read32) { return GIC_STATE; }uint32_t bit=1u<<s->description.timer_irq,enabled;
    /* Also permitted after a failed transaction so runtime can quiesce the
     * owned PPI while CPU IRQs and the architectural timer stay masked. */
    if (!write(s,false,D_DISABLE,bit) || !write(s,false,D_PENDING_CLEAR,bit) || !read(s,false,D_ENABLE,&enabled)) { return fail(s,GIC_IO); }
    if (enabled&bit) { return fail(s,GIC_STATE); }s->timer_enabled=false;return GIC_OK;
}
enum gic_result gic_ack(struct gic_state *s,uint32_t *raw)
{
    if (!s || !raw || !s->ready) { return GIC_STATE; }uint32_t value;
    if (!read(s,true,C_IAR,&value)) { return fail(s,GIC_IO); }
    ++s->acknowledgements;s->last_token=value;s->last_id=value&1023;*raw=value;return GIC_OK;
}
enum gic_result gic_eoi(struct gic_state *s,uint32_t raw)
{
    if (!s || !s->ready || (raw&1023)>=1020) { return GIC_STATE; }
    if (!write(s,true,C_EOI,raw)) { return fail(s,GIC_IO); }++s->completions;return GIC_OK;
}
enum gic_result gic_mask_unexpected(struct gic_state *s,unsigned id)
{
    if (!s || !s->ready || id>=1020 || id>=32*((s->typer&31)+1)) { return GIC_ARGUMENT; }
    /* SGIs cannot be disabled through ICENABLER; caller must stop IRQ
     * delivery on an unowned SGI rather than claim it was masked. */
    if (id<16) { return GIC_UNSUPPORTED; }
    uint32_t bit=1u<<(id&31),enabled;
    if (!write(s,false,D_DISABLE+4*(id/32),bit) || !read(s,false,D_ENABLE+4*(id/32),&enabled)) { return fail(s,GIC_IO); }
    if (enabled&bit) { return fail(s,GIC_STATE); }++s->masked_unexpected;return GIC_OK;
}
const char *gic_error(enum gic_result r)
{ switch (r) { case GIC_OK:return "ok";case GIC_ARGUMENT:return "invalid GIC argument";case GIC_UNSUPPORTED:return "unsupported GIC/CPU/active timer state";case GIC_IO:return "GIC register I/O error";default:return "GIC not ready or register readback differs"; } }
