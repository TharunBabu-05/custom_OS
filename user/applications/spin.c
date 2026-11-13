#include "user.h"
int app_main(void)
{
    (void)uos_puts("EL0 timeout test: infinite user loop.\n");
    for (;;) { __asm__ volatile("nop"); }
}
