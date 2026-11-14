#include "user.h"
int app_main(void)
{
    struct app_args args={0};const char *path="/boot/TTYCHK.ELF";args.count=1;
    do { args.bytes[args.used++]=*path; }while (*path++);
    int64_t pid=uos_spawn(&args,5);if (pid<0 || uos_sleep_ms(100)) { return 1; }
    uos_puts("Parent now waits for the early terminal reader.\n");
    if (uos_wait((uint64_t)pid)) { return 2; }
    uos_puts("PASS: child readline before parent wait blocks until terminal handoff.\n");return 0;
}
