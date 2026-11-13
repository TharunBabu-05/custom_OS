#include "user.h"
int app_main(void)
{
    (void)uos_puts("EL0 protection test: reading an EL1-only kernel mapping.\n");
    uint64_t address=0x40080000ULL;
    __asm__ volatile("ldr x0, [%0]" :: "r"(address) : "x0","memory");
    (void)uos_puts("FAIL: EL0 read of kernel mapping returned.\n");return 1;
}
