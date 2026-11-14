#include "user.h"
int app_main(void)
{
    struct app_args a={0};const char *path="/boot/SPIN.ELF";a.count=1;
    do { a.bytes[a.used++]=*path; }while (*path++);
    if (uos_spawn(&a,30)<0) { return 1; }
    uos_puts("Parent exits with live child; kernel must cancel and collect it.\n");return 0;
}
