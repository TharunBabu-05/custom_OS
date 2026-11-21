#ifndef OS_IRQ_WORK_H
#define OS_IRQ_WORK_H
#include <stdbool.h>
#include <stdint.h>

enum { IRQ_WORK_SLOTS=4, IRQ_WORK_PENDING_MAX=8 };
enum irq_work_result { IRQ_WORK_UNBOUND, IRQ_WORK_QUEUED, IRQ_WORK_MASK };
typedef bool (*irq_source_ack)(void *);
typedef void (*irq_deferred_fn)(void *);
struct irq_work_binding {
    unsigned intid,pending;
    bool quarantined;
    irq_source_ack acknowledge;
    irq_deferred_fn deferred;
    void *context;
};
struct irq_work {
    struct irq_work_binding bindings[IRQ_WORK_SLOTS];
    unsigned next;
    uint64_t received,serviced,overflow,stuck;
};
/* Call with CPU IRQs masked. An acknowledge callback clears the device
 * level and returns true once it is quiescent. Deferred callbacks run
 * separately on the foreground stack after irq_work_take. */
bool irq_work_bind(struct irq_work *,unsigned,irq_source_ack,irq_deferred_fn,void *);
bool irq_work_unbind(struct irq_work *,unsigned);
enum irq_work_result irq_work_receive(struct irq_work *,unsigned);
irq_deferred_fn irq_work_take(struct irq_work *,void **context);
#endif
