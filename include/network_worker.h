#ifndef OS_NETWORK_WORKER_H
#define OS_NETWORK_WORKER_H
#include <stdbool.h>

/* Single cooperative owner. Start, service and stop run on the kernel
 * translation root outside exception dispatch. Only the worker may pause. */
bool network_worker_start(void (*entry)(void *),void *argument);
bool network_worker_active(void);
bool network_worker_pause(void);
bool network_worker_due(void);
void network_worker_service(void);
bool network_worker_stop(void);
bool network_worker_test(bool with_user);
#endif
