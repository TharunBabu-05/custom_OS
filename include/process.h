#ifndef OS_PROCESS_H
#define OS_PROCESS_H
#include "exception.h"
#include "app_args.h"
enum { PROCESS_LIMIT=5 };
/* Foreground EL0 batch; timer IRQs preempt users, never kernel drivers.
 * 0 = all exited successfully, 1 = contained process failure, 2 = refused. */
int process_run(const char *const *paths,unsigned count);
int process_exec(const struct app_args *args);
int process_terminal(const struct app_args *args,unsigned seconds);
/* Dedicated boot shell session. An image replacing the shell gets a deadline. */
int process_shell(void);
bool process_tools_test(void);
void process_info(void);
bool process_service_start(const struct app_args *args);
void process_service_stop(void);
void process_service_info(void);
void process_service_step(void);
bool process_test(void);
bool process_command(const char *line);
bool app_file_command(const char *verb,const char *arguments);
/* Called only for lower-AArch64 sync/IRQ frames after IRQ acknowledgement. */
bool process_exception(struct exception_frame *,bool timer_tick,bool irq_ok);
#endif
