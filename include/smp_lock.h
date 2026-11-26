#ifndef OS_SMP_LOCK_H
#define OS_SMP_LOCK_H
#include "smp.h"
#include <stdint.h>

enum { SMP_LOCK_SLOTS=SMP_WORKER_LIMIT+1, SMP_LOCK_BOOT=SMP_WORKER_LIMIT };
struct smp_lock { uint32_t tickets[SMP_LOCK_SLOTS]; };

/* One writer per slot. Only foreground EL1 code may acquire it. Zero the
 * lock before use or after all users are known to have stopped. */
bool smp_lock_acquire(struct smp_lock *,unsigned slot,uint64_t start,uint64_t budget);
void smp_lock_release(struct smp_lock *,unsigned slot);
bool smp_lock_idle(const struct smp_lock *);
#endif
