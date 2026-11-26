#include "user.h"
int app_main(void)
{
    unsigned seconds=5;
    if (uos_argc()>1) {
        char mode[7];
        if (uos_argc()!=2 || uos_arg(1,mode,sizeof(mode))!=6) { return 1; }
        for (unsigned i=0;i<6;++i) { if (mode[i]!="manual"[i]) { return 1; } }
        /* Leave time to report an expired reader before runio's 30 s ends. */
        seconds=25;
    }
    struct app_args args={0};const char *path="/boot/TTYCHK.ELF";args.count=1;
    do { args.bytes[args.used++]=*path; }while (*path++);
    int64_t pid=uos_spawn(&args,seconds);if (pid<0 || uos_sleep_ms(100)) { return 1; }
    uos_puts("Parent now waits for the early terminal reader.\n");
    int64_t code=uos_wait((uint64_t)pid);
    if (code) {
        uos_puts(code==124 ? "TTYPAR: reader timed out.\n" :
                 code==130 ? "TTYPAR: reader cancelled.\n" : "TTYPAR: terminal read check failed.\n");
        return 2;
    }
    uos_puts("PASS: child readline before parent wait blocks until terminal handoff.\n");return 0;
}
