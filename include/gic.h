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
struct gic_spi_route { unsigned intid;uint8_t priority,target;bool edge,restore_trigger; };
enum { GIC_WORK_SGI=15 };
struct gic_sgi_route {
    uint32_t control,mask;
    uint8_t target,priority;
    bool enabled,saved;
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
/* Claim an initially disabled, inactive, level-triggered SPI. The owner
 * quiesces its device before enable and before release. CPU IRQs stay masked. */
enum gic_result gic_spi_claim(struct gic_state *,unsigned id,struct gic_spi_route *);
/* Edge-mode claim for an unused SPI. The previous trigger mode is restored. */
enum gic_result gic_spi_claim_edge(struct gic_state *,unsigned id,struct gic_spi_route *);
enum gic_result gic_spi_test_pending(struct gic_state *,const struct gic_spi_route *);
enum gic_result gic_spi_enable(struct gic_state *,const struct gic_spi_route *);
enum gic_result gic_spi_release(struct gic_state *,const struct gic_spi_route *);
enum gic_result gic_mask_line(struct gic_state *,unsigned id);
/* Banked secondary interface. Call open/close with local IRQs masked.
 * Only the immutable description, I/O callbacks and TYPER are shared. */
uint8_t gic_sgi_target(const struct gic_state *);
bool gic_sgi_open(const struct gic_state *,struct gic_sgi_route *);
bool gic_sgi_close(const struct gic_state *,struct gic_sgi_route *);
bool gic_sgi_send(const struct gic_state *,uint8_t targets);
bool gic_sgi_take(const struct gic_state *,uint32_t *token);
const char *gic_error(enum gic_result);
#endif
