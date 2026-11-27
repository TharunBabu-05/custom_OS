#ifndef OS_SMP_H
#define OS_SMP_H

#define SMP_WORKER_LIMIT 3
#define SMP_WORKER_SIZE 256

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

struct exception_frame;
struct smp_worker;

static inline struct smp_worker *smp_current_worker(void)
{
    struct smp_worker *worker;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(worker));
    return worker;
}

void smp_worker_exception(struct smp_worker *,struct exception_frame *);
bool smp_command(const char *line,const void *dtb);
bool smp_shutdown(void);
struct smp_worker_counts { unsigned ready,parked,faulted,transitioning; };
struct smp_worker_counts smp_count_workers(void);

struct smp_user_slice {
    struct exception_frame *frame;
    uint64_t root,stack,thread_pointer,quantum,begin,end;
    bool irq_ok;
};
/* Preflight routed workers, then run one frame per worker. Every worker
 * returns to the kernel root before the boot CPU may service any frame. */
unsigned smp_user_workers(void);
void smp_user_round(struct smp_user_slice *,unsigned count,uint64_t kernel_root);
#endif
#endif
