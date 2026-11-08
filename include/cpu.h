#ifndef OS_CPU_H
#define OS_CPU_H
#include <stdint.h>
static inline unsigned long cpu_el(void)
{
    unsigned long value;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(value));
    return value >> 2;
}
static inline void cpu_halt(void)
{
    for (;;) { __asm__ volatile ("wfi"); }
}
uint64_t timer_ticks(void);
uint64_t timer_frequency(void);
#endif
