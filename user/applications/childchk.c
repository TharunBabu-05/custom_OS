#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
int app_main(void)
{
    char text[256];struct app_args a={0};uint64_t handle=0;
    if (uos_argc()!=4 || uos_arg(1,text,sizeof(text))<1) { return 1; }
    for (unsigned i=0;text[i];++i) { if (text[i]<'0' || text[i]>'9') { return 2; }handle=handle*10+(unsigned)(text[i]-'0'); }
    if (uos_read(handle,text,1)!=UOS_ERROR || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc")) { return 3; }
    if (uos_arg(2,text,sizeof(text))!=11 || !same(text,"hello world") || uos_arg(3,text,sizeof(text))!=0) { return 4; }
    if (uos_chdir("/tmp") || uos_open("/etc/hostname",UOS_READ)<0 || uos_spawn(&a,1)!=UOS_ERROR || uos_wait((uint64_t)uos_getpid())!=UOS_ERROR) { return 5; }
    return 37;
}
