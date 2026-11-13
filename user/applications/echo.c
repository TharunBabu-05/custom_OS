#include "user.h"
int app_main(void)
{
    char text[512];int64_t argc=uos_argc();
    for (unsigned i=1;i<(unsigned)argc;++i) {
        int64_t n=uos_arg(i,text,sizeof(text));if (n<0) { return 1; }
        if (i>1) { uos_puts(" "); }uos_write(text,(size_t)n);
    }
    uos_puts("\n");return 0;
}
