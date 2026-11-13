#include "user.h"
int app_main(void)
{
    (void)uos_puts("EL0 protection test: executing its non-executable stack.\n");
    uint32_t instruction=0xd65f03c0u; /* RET x30 if the NX check were broken. */
    __asm__ volatile("blr %0" :: "r"(&instruction) : "x30","memory","cc");
    (void)uos_puts("FAIL: EL0 execution from stack returned.\n");return 1;
}
