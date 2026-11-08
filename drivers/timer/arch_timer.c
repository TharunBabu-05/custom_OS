#include "cpu.h"
uint64_t timer_ticks(void)
{
    uint64_t value;
    __asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r"(value));
    return value;
}
uint64_t timer_frequency(void)
{
    uint64_t value;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}
