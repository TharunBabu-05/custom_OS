#ifndef OS_SMP_MEMORY_H
#define OS_SMP_MEMORY_H
#include <stdbool.h>

enum { SMP_MEMORY_MAX_ROUNDS=16, SMP_MEMORY_DEFAULT_ROUNDS=4 };

/* The boot CPU services the mailboxes and owns all lease metadata and PMM
 * changes. Each secondary uses only its own mailbox and granted spans. */
bool smp_memory_begin(unsigned workers);
void smp_memory_service(void);
void smp_memory_abort(void);
bool smp_memory_finish(unsigned acknowledged);
bool smp_memory_reclaim(unsigned slot);
void smp_memory_print(unsigned slot);

/* The worker discards all aliases before returning, including on errors. */
bool smp_memory_worker(unsigned slot,unsigned rounds);
#endif
