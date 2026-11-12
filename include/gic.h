#ifndef OS_GIC_H
#define OS_GIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct gic_description { uint64_t distributor,cpu;uint32_t distributor_span,cpu_span;unsigned timer_irq; };
struct gic_io {
    void *context;
    bool (*read32)(void *,bool cpu,unsigned offset,uint32_t *);
    bool (*write32)(void *,bool cpu,unsigned offset,uint32_t);
};
enum gic_result { GIC_OK,GIC_ARGUMENT,GIC_UNSUPPORTED,GIC_IO,GIC_STATE };
struct gic_state {
    struct gic_description description;struct gic_io io;
    bool attempted,ready,timer_enabled,changed;enum gic_result result;
    uint32_t typer,pidr2,distributor_ctlr,cpu_ctlr,priority_mask,cpu_targets;
    unsigned acknowledgements,completions,masked_unexpected,last_id;uint32_t last_token;
};
/* Only the explicitly supported Pi5/QEMU DT profiles, no MMIO. */
bool gic_discover(const char *board,const void *dtb,size_t bytes,struct gic_description *out);
/* Caller masks CPU IRQs and disables the architectural timer first. Single CPU.
 * Group/security configuration is retained from firmware. No global SPI reset. */
enum gic_result gic_initialize(struct gic_state *,const struct gic_description *,const struct gic_io *);
enum gic_result gic_timer_enable(struct gic_state *);
enum gic_result gic_timer_disable(struct gic_state *);
enum gic_result gic_ack(struct gic_state *,uint32_t *raw);
enum gic_result gic_eoi(struct gic_state *,uint32_t raw);
enum gic_result gic_mask_unexpected(struct gic_state *,unsigned id);
const char *gic_error(enum gic_result);
#endif
