#ifndef OS_NETWORK_WORKER_H
#define OS_NETWORK_WORKER_H
#include <stdbool.h>
#include <stdint.h>

/* Single cooperative owner. Start, service and stop run on the kernel
 * translation root outside exception dispatch. Only the worker may pause. */
bool network_worker_start(void (*entry)(void *),void *argument);
bool network_worker_active(void);
bool network_worker_pause(void);
bool network_worker_due(void);
void network_worker_service(void);
bool network_worker_stop(void);
/* One optional DMA backing page, held until hardware teardown is verified. */
bool network_worker_dma_alloc(uint64_t *physical);
bool network_worker_dma_free(uint64_t physical);
unsigned network_worker_dma_pages(void);
bool network_worker_test(bool with_user);
bool network_worker_cleanup_test(void);
#ifndef OUROS_PI5
bool udp_worker_test(void);
bool tcp_worker_test(void);
bool tcp_worker_slow_test(void);
#endif
#endif
