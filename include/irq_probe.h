#ifndef OS_IRQ_PROBE_H
#define OS_IRQ_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum irq_probe_kind { IRQ_PROBE_BCM_TIMER, IRQ_PROBE_PL031 };
struct irq_probe {
    uintptr_t base;
    unsigned intid;
    enum irq_probe_kind kind;
    uint32_t old_compare;
};
bool irq_probe_discover(const char *board,const void *dtb,size_t bytes,struct irq_probe *);
bool irq_probe_available(struct irq_probe *);
void irq_probe_arm(const struct irq_probe *);
void irq_probe_restore(const struct irq_probe *);
bool irq_probe_ack(void *);
#endif
