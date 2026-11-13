#include "user.h"
int app_main(void)
{
    static const char hello[]="Hello from an OurOS EL0 application.\n";
    return uos_write(hello,sizeof(hello)-1)==(int64_t)(sizeof(hello)-1) ? 0 : 1;
}
