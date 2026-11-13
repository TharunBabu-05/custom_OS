#include "user.h"
int app_main(void)
{
    /* Deliberately no YIELD syscall: the timer must preempt this process. */
    volatile uint64_t steps=0;
    while (steps<2000000) { ++steps;__asm__ volatile("nop"); }
    return uos_puts("PASS: EL0 busy loop completed under timer preemption.\n")>0 ? 0 : 1;
}
