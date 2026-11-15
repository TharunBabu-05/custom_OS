#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
int app_main(void)
{
    char token[24],out[128];unsigned used=0;uint64_t pid=(uint64_t)uos_getpid();
    do { token[used++]=(char)('0'+pid%10);pid/=10; }while (pid);token[used]=0;
    if (uos_getenv("ISOLATED",out,sizeof(out))!=UOS_ENOENT || uos_setenv("ISOLATED",token)) { return 1; }
    for (unsigned i=0;i<16;++i) {
        if (uos_yield() || uos_sleep_ms(10) || uos_getenv("ISOLATED",out,sizeof(out))!=(int64_t)used || !same(out,token)) { return 2; }
    }
    uos_puts("PASS: concurrent root environments remain private across yields and sleeps.\n");return 0;
}
