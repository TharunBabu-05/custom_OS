#include "user.h"
int app_main(void)
{
    (void)uos_puts("EL0 protection test: writing its read-only executable page.\n");
    uint64_t address=0x2000000000ULL;
    __asm__ volatile("str xzr, [%0]" :: "r"(address) : "memory");
    (void)uos_puts("FAIL: EL0 write to executable page returned.\n");return 1;
}
