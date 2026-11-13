#include "user.h"
int app_main(void)
{
    if (uos_open("/etc/hostname",UOS_READ)<0) { return 1; }
    (void)uos_puts("EL0 exit cleanup test: leaving one owned handle open.\n");return 0;
}
