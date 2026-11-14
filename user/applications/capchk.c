#include "user.h"
int app_main(void)
{
    struct app_args a={0};const char *path="/boot/HELLO.ELF";a.count=1;
    do { a.bytes[a.used++]=*path; }while (*path++);
    if (uos_spawn(&a,1)!=UOS_ERROR) { return 1; }
    uos_puts("PASS: full process table refuses child creation.\n");return 0;
}
