#ifndef OS_IRQ_H
#define OS_IRQ_H
#include <stdint.h>
#include <stdbool.h>
void irq_setup(const char *board,uintptr_t dtb);
bool irq_command(const char *line);
bool irq_dispatch(void);
uint64_t irq_save(void);
void irq_restore(uint64_t saved);
void irq_fatal_stop(void);
bool irq_timer_running(void);
#endif
