#ifndef OS_REBOOT_H
#define OS_REBOOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum reset_method { RESET_BCM2712, RESET_PSCI_HVC, RESET_PSCI_SMC };
struct reset_target { enum reset_method method; uintptr_t registers; };

bool reset_discover(const char *board,const void *dtb,size_t bytes,struct reset_target *out);
/* Issued after storage is quiescent and CPU IRQs are masked. */
void reset_request(const struct reset_target *target);
void system_reboot(const char *board,uintptr_t dtb);
#endif
