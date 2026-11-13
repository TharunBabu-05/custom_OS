#include "user.h"
int app_main(void)
{
    char text[16];unsigned ms=0;
    if (uos_argc()!=2 || uos_arg(1,text,sizeof(text))<=0) { uos_puts("usage: SLEEP.ELF <0..1000 milliseconds>\n");return 2; }
    for (unsigned i=0;text[i];++i) { if (text[i]<'0' || text[i]>'9' || ms>1000) { return 2; }ms=ms*10+(unsigned)(text[i]-'0'); }
    if (ms>1000 || uos_sleep_ms(ms)) { return 2; }uos_puts("sleep complete\n");return 0;
}
