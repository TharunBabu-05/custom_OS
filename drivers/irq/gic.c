/* GICv2 non-secure interface. Per-source changes preserve neighboring
 * interrupt settings and firmware security-group ownership.
 * References: Arm IHI0048B GICv2; NS EOImodeNS is GICC_CTLR bit 9. */
#include "gic.h"
enum { D_CTLR=0,D_TYPER=4,D_ENABLE=0x100,D_DISABLE=0x180,D_PENDING=0x200,D_PENDING_CLEAR=0x280,D_ACTIVE=0x300,
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
    /* A source-mask readback can fail after an acknowledge. Retire its
     * raw token before the caller masks CPU IRQs and stops dispatch. */
    if (!s || !s->attempted || (raw&1023)>=1020 || !s->io.write32) { return GIC_STATE; }
    if (!write(s,true,C_EOI,raw)) { return fail(s,GIC_IO); }++s->completions;return GIC_OK;
}
static bool implemented(const struct gic_state *s,unsigned id)
{ return id<1020 && id<32*((s->typer&31)+1); }
enum gic_result gic_mask_line(struct gic_state *s,unsigned id)
{
    if (!s || !s->ready || !implemented(s,id)) { return GIC_ARGUMENT; }
    /* SGIs cannot be disabled through ICENABLER; caller must stop IRQ
     * delivery on an unowned SGI rather than claim it was masked. */
    if (id<16) { return GIC_UNSUPPORTED; }
    uint32_t bit=1u<<(id&31),enabled;
    if (!write(s,false,D_DISABLE+4*(id/32),bit) || !read(s,false,D_ENABLE+4*(id/32),&enabled)) { return fail(s,GIC_IO); }
    if (enabled&bit) { return fail(s,GIC_STATE); }return GIC_OK;
}
enum gic_result gic_mask_unexpected(struct gic_state *s,unsigned id)
{
    enum gic_result result=gic_mask_line(s,id);
    if (result==GIC_OK) { ++s->masked_unexpected; }
    return result;
}
static bool write_byte(struct gic_state *s,unsigned offset,unsigned id,uint8_t byte)
{
    unsigned reg=offset+4*(id/4),shift=(id&3)*8;
    uint32_t value;
    return read(s,false,reg,&value) && write(s,false,reg,(value&~(255u<<shift))|((uint32_t)byte<<shift));
}
static uint8_t local_target(const struct gic_state *s)
{
    uint32_t bytes=s->cpu_targets;
    return (uint8_t)(bytes|(bytes>>8)|(bytes>>16)|(bytes>>24));
}
enum gic_result gic_spi_release(struct gic_state *s,const struct gic_spi_route *route)
{
    if (!s || !s->attempted || !route || route->intid<32 || !implemented(s,route->intid)) { return GIC_ARGUMENT; }
    unsigned id=route->intid,bank=4*(id/32),field=4*(id/4),shift=(id&3)*8;
    uint32_t bit=1u<<(id&31),enabled,priority,target=0;
    bool programmable=local_target(s)!=0;
    if (!write(s,false,D_DISABLE+bank,bit) || !read(s,false,D_ENABLE+bank,&enabled)) { return fail(s,GIC_IO); }
    if (enabled&bit) { return fail(s,GIC_STATE); }
    if (!write(s,false,D_PENDING_CLEAR+bank,bit) || !write_byte(s,D_PRIORITY,id,route->priority) ||
        !read(s,false,D_PRIORITY+field,&priority) || (programmable &&
        (!write_byte(s,D_TARGET,id,route->target) || !read(s,false,D_TARGET+field,&target)))) { return fail(s,GIC_IO); }
    if ((uint8_t)(priority>>shift)!=route->priority ||
        (programmable && (uint8_t)(target>>shift)!=route->target)) { return fail(s,GIC_STATE); }
    return GIC_OK;
}
enum gic_result gic_spi_claim(struct gic_state *s,unsigned id,struct gic_spi_route *route)
{
    if (!s || !s->ready || !route || id<32 || !implemented(s,id)) { return GIC_ARGUMENT; }
    unsigned bank=4*(id/32),field=4*(id/4),shift=(id&3)*8;
    uint32_t bit=1u<<(id&31),enabled,active,pending,config,priority,target=0;
    uint8_t local=local_target(s);
    if (!read(s,false,D_ENABLE+bank,&enabled) || !read(s,false,D_ACTIVE+bank,&active) ||
        !read(s,false,D_PENDING+bank,&pending) || !read(s,false,D_CONFIG+4*(id/16),&config) ||
        !read(s,false,D_PRIORITY+field,&priority) || (local && !read(s,false,D_TARGET+field,&target))) { return GIC_IO; }
    if (((enabled|active|pending)&bit) || (config&(2u<<((id&15)*2)))) { return GIC_STATE; }
    struct gic_spi_route saved={id,(uint8_t)(priority>>shift),(uint8_t)(target>>shift)};
    /* A zero target field is valid only for a single-CPU controller with
     * fixed routing. Its target registers read as zero and ignore writes. */
    enum gic_result result=GIC_IO;
    if (write_byte(s,D_PRIORITY,id,0xa0) && read(s,false,D_PRIORITY+field,&priority) &&
        (!local || (write_byte(s,D_TARGET,id,local) && read(s,false,D_TARGET+field,&target)))) {
        if (((priority>>shift)&0xf0)==0xa0 && (!local || (uint8_t)(target>>shift)==local)) {
            *route=saved;return GIC_OK;
        }
        result=GIC_STATE;
    }
    return gic_spi_release(s,&saved)==GIC_OK ? result : s->result;
}
enum gic_result gic_spi_enable(struct gic_state *s,const struct gic_spi_route *route)
{
    if (!s || !s->ready || !route || route->intid<32 || !implemented(s,route->intid)) { return GIC_ARGUMENT; }
    unsigned id=route->intid,bank=4*(id/32);
    uint32_t bit=1u<<(id&31),enabled;
    if (!write(s,false,D_ENABLE+bank,bit) || !read(s,false,D_ENABLE+bank,&enabled)) { return GIC_IO; }
    return enabled&bit ? GIC_OK : GIC_STATE;
}
const char *gic_error(enum gic_result r)
{ switch (r) { case GIC_OK:return "ok";case GIC_ARGUMENT:return "invalid GIC argument";case GIC_UNSUPPORTED:return "unsupported GIC/CPU/active timer state";case GIC_IO:return "GIC register I/O error";default:return "GIC not ready or register readback differs"; } }
