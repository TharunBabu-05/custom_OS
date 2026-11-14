#include "user.h"
int app_main(void)
{
    char line[32];line[0]='!';
    if (uos_readline(line,1)!=UOS_ERROR || line[0]!='!' || uos_readline(line,129)!=UOS_ERROR ||
        uos_readline((char *)0,32)!=UOS_EFAULT || uos_readline((char *)0x2000000000ULL,32)!=UOS_EFAULT) { return 1; }
    uos_puts("TTYCHK ready: enter terminal-ok\n");
    /* This handle must also be released if Ctrl+C/timeout ends the wait. */
    if (uos_open("/etc/hostname",UOS_READ)<0) { return 4; }
    int64_t n=uos_readline(line,sizeof(line));const char *expected="terminal-ok";
    if (n!=11 || line[11]) { return 2; }for (unsigned i=0;i<11;++i) { if (line[i]!=expected[i]) { return 3; } }
    uos_puts("PASS: terminal pointer/capacity checks and blocking line delivery.\n");return 0;
}
