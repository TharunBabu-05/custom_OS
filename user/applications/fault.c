#include "user.h"
int app_main(void)
{
    (void)uos_puts("EL0 fault test: reading unmapped address zero.\n");
    __asm__ volatile("mov x9, xzr\n\tldr x0, [x9]" ::: "x0","x9","memory");
    (void)uos_puts("FAIL: null read unexpectedly returned.\n");return 1;
}
