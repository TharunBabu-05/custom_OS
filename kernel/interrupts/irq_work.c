#include "irq_work.h"

static struct irq_work_binding *binding(struct irq_work *work,unsigned intid)
{
    for (unsigned i=0;i<IRQ_WORK_SLOTS;++i) {
        struct irq_work_binding *entry=&work->bindings[i];
        if (entry->acknowledge && entry->intid==intid) { return entry; }
    }
    return 0;
}
bool irq_work_bind(struct irq_work *work,unsigned intid,irq_source_ack acknowledge,irq_deferred_fn deferred,void *context)
{
    if (intid<32 || intid>=1020 || !acknowledge || !deferred || binding(work,intid)) { return false; }
    for (unsigned i=0;i<IRQ_WORK_SLOTS;++i) {
        if (!work->bindings[i].acknowledge) {
            work->bindings[i]=(struct irq_work_binding){.intid=intid,.acknowledge=acknowledge,.deferred=deferred,.context=context};
            return true;
        }
    }
    return false;
}
bool irq_work_unbind(struct irq_work *work,unsigned intid)
{
    struct irq_work_binding *entry=binding(work,intid);
    if (!entry || entry->pending) { return false; }
    *entry=(struct irq_work_binding){0};
    return true;
}
enum irq_work_result irq_work_receive(struct irq_work *work,unsigned intid)
{
    struct irq_work_binding *entry=binding(work,intid);
    if (!entry) { return IRQ_WORK_UNBOUND; }
    if (entry->quarantined) { return IRQ_WORK_MASK; }
    ++work->received;
    if (!entry->acknowledge(entry->context)) {
        entry->quarantined=true;++work->stuck;return IRQ_WORK_MASK;
    }
    if (entry->pending==IRQ_WORK_PENDING_MAX) {
        entry->quarantined=true;++work->overflow;return IRQ_WORK_MASK;
    }
    ++entry->pending;
    return IRQ_WORK_QUEUED;
}
irq_deferred_fn irq_work_take(struct irq_work *work,void **context)
{
    for (unsigned i=0;i<IRQ_WORK_SLOTS;++i) {
        unsigned index=(work->next+i)%IRQ_WORK_SLOTS;
        struct irq_work_binding *entry=&work->bindings[index];
        if (entry->pending) {
            --entry->pending;++work->serviced;
            work->next=(index+1)%IRQ_WORK_SLOTS;
            *context=entry->context;
            return entry->deferred;
        }
    }
    return 0;
}
