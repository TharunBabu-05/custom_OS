#ifndef OS_PROCESS_H
#define OS_PROCESS_H
#include "exception.h"
#include "app_args.h"
enum { PROCESS_LIMIT=3 };
/* Foreground EL0 batch; timer IRQs preempt users, never kernel drivers.
 * 0 = all exited successfully, 1 = contained process failure, 2 = refused. */
int process_run(const char *const *paths,unsigned count);
int process_exec(const struct app_args *args);
bool process_tools_test(void);
void process_info(void);
bool process_test(void);
bool process_command(const char *line);
/* Called only for lower-AArch64 sync/IRQ frames after IRQ acknowledgement. */
bool process_exception(struct exception_frame *,bool timer_tick,bool irq_ok);
#endif
