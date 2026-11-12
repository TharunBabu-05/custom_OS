#ifndef OS_TASK_H
#define OS_TASK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { TASK_LIMIT=4,TASK_STACK_BYTES=16384,TASK_GUARD_BYTES=4096,TASK_STACK_STRIDE=20480 };
enum task_status { TASK_UNUSED,TASK_READY,TASK_RUNNING,TASK_SLEEPING,TASK_EXITED,TASK_CANCELLED };
enum task_result { TASK_OK,TASK_ARGUMENT,TASK_STATE,TASK_FULL,TASK_OVERFLOW };
struct task_record { enum task_status status;uint64_t deadline,dispatches,yields,sleeps,wakes;int exit_code; };
struct task_queue { struct task_record tasks[TASK_LIMIT];int current;unsigned cursor;uint64_t switches; };
/* Pure single-core round-robin policy. All runtime calls are made with IRQs
 * masked. Timer IRQs do not modify this queue or cause a context switch. */
void task_queue_init(struct task_queue *);
enum task_result task_add(struct task_queue *,unsigned *id);
int task_pick(struct task_queue *,uint64_t now);
enum task_result task_yield_policy(struct task_queue *);
enum task_result task_sleep_policy(struct task_queue *,uint64_t now,uint64_t delay);
enum task_result task_exit_policy(struct task_queue *,int exit_code);
bool task_queue_done(const struct task_queue *);
void task_cancel_all(struct task_queue *);
const char *task_status_text(enum task_status);
/* AAPCS64 procedure-call context: x19..x30 and SP. Context transfers only at
 * explicit calls, not at arbitrary interrupted instructions. SIMD disabled. */
struct task_context { uint64_t x[12],sp; };
_Static_assert(offsetof(struct task_context,sp)==96,"context SP offset");
extern unsigned char task_stacks[TASK_LIMIT][TASK_STACK_STRIDE];
bool task_command(const char *);
bool task_yield(void);
bool task_sleep_ms(unsigned milliseconds);
void task_exit(int code) __attribute__((noreturn));
void task_context_switch(struct task_context *old,const struct task_context *next);
uint64_t task_context_selftest(unsigned rounds);
#endif
