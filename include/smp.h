#ifndef OS_SMP_H
#define OS_SMP_H

#define SMP_WORKER_LIMIT 3
#define SMP_WORKER_SIZE 256

#ifndef __ASSEMBLER__
#include <stdbool.h>

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
#endif
#endif
