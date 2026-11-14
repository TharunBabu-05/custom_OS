#include "user.h"
int app_main(void)
{
    char path[256],bytes[512];
    int64_t argc=uos_argc();if ((argc!=1 && argc!=2) || (argc==2 && uos_arg(1,path,sizeof(path))<=0)) { uos_puts("usage: CAT.ELF [file|-]\n");return 2; }
    int standard=argc==1 || (path[0]=='-' && !path[1]);
    int64_t h=standard ? 0 : uos_open(path,UOS_READ);if (h<0) { uos_puts("cat: cannot open file\n");return 1; }
    int result=0;
    for (;;) {
        int64_t n=standard ? uos_stdin(bytes,sizeof(bytes)) : uos_read((uint64_t)h,bytes,sizeof(bytes));if (n<0) { uos_puts("cat: read failed\n");result=1;break; }
        if (!n) { break; }if (uos_write(bytes,(size_t)n)!=n) { result=1;break; }
    }
    if (!standard && uos_close((uint64_t)h)) { result=1; }return result;
}
