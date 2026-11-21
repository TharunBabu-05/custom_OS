#include "irq_probe.h"

enum { BCM_STATUS=0, BCM_LOW=4, BCM_COMPARE3=0x18, BCM_MATCH3=1<<3 };
enum { RTC_DATA=0, RTC_MATCH=4, RTC_MASK=0x10, RTC_RAW=0x14, RTC_CLEAR=0x1c, RTC_ALARM=1 };

static uint32_t read(const struct irq_probe *probe,unsigned offset)
{ return *(volatile uint32_t *)(probe->base+offset); }
static void write(const struct irq_probe *probe,unsigned offset,uint32_t value)
{
    *(volatile uint32_t *)(probe->base+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
bool irq_probe_available(struct irq_probe *probe)
{
    if (probe->kind==IRQ_PROBE_BCM_TIMER) {
        if (read(probe,BCM_STATUS)&BCM_MATCH3) { return false; }
        probe->old_compare=read(probe,BCM_COMPARE3);
    } else {
        if ((read(probe,RTC_RAW)|read(probe,RTC_MASK))&RTC_ALARM) { return false; }
        probe->old_compare=read(probe,RTC_MATCH);
    }
    return true;
}
void irq_probe_arm(const struct irq_probe *probe)
{
    if (probe->kind==IRQ_PROBE_BCM_TIMER) {
        write(probe,BCM_COMPARE3,read(probe,BCM_LOW)+50000u);
    } else {
        write(probe,RTC_MATCH,read(probe,RTC_DATA)+1u);
        write(probe,RTC_MASK,RTC_ALARM);
    }
}
bool irq_probe_ack(void *context)
{
    const struct irq_probe *probe=context;
    if (probe->kind==IRQ_PROBE_BCM_TIMER) {
        if (!(read(probe,BCM_STATUS)&BCM_MATCH3)) { return false; }
        write(probe,BCM_STATUS,BCM_MATCH3);
        return !(read(probe,BCM_STATUS)&BCM_MATCH3);
    }
    if (!(read(probe,RTC_RAW)&RTC_ALARM)) { return false; }
    write(probe,RTC_MASK,0);
    write(probe,RTC_CLEAR,RTC_ALARM);
    return !(read(probe,RTC_RAW)&RTC_ALARM);
}
void irq_probe_restore(const struct irq_probe *probe)
{
    if (probe->kind==IRQ_PROBE_BCM_TIMER) {
        write(probe,BCM_COMPARE3,probe->old_compare);
        write(probe,BCM_STATUS,BCM_MATCH3);
    } else {
        write(probe,RTC_MASK,0);
        write(probe,RTC_MATCH,probe->old_compare);
        write(probe,RTC_CLEAR,RTC_ALARM);
    }
}
