#include "user.h"
int app_main(void)
{
    volatile uint64_t live[16];int64_t pid=uos_getpid();if (pid<=0) { return 1; }
    int64_t h=uos_open("/etc/hostname",UOS_READ);if (h<0) { return 2; }
    for (unsigned i=0;i<16;++i) { live[i]=(uint64_t)pid+i*13; }
    if (uos_sleep_ms(1001)!=UOS_ERROR || uos_sleep_ms(0)) { return 3; }
    for (unsigned step=0;step<3;++step) {
        int64_t before=uos_time_ms();if (uos_sleep_ms(40+step*10) || uos_time_ms()-before<40+step*10) { return 4; }
        if (uos_getpid()!=pid) { return 5; }
        for (unsigned i=0;i<16;++i) { if (live[i]!=(uint64_t)pid+i*13) { return 6; } }
    }
    char byte;if (uos_read((uint64_t)h,&byte,1)!=1 || byte!='o') { return 7; }
    /* Deliberately leave handle for process exit cleanup. */
    uos_puts("PASS: EL0 sleep retained stack, PID and file ownership.\n");return 0;
}
