#include "user.h"
int app_main(void)
{
    if (uos_argc()==2) {
        char key[APP_ENV_NAME_BYTES],value[APP_ENV_VALUE_BYTES];
        if (uos_arg(1,key,sizeof(key))<0 || uos_getenv(key,value,sizeof(value))<0) { uos_puts("env: variable unavailable\n");return 1; }
        uos_puts(value);uos_puts("\n");return 0;
    }
    if (uos_argc()!=1) { uos_puts("Usage: ENV.ELF [name]\n");return 2; }
    struct app_env_entry e;
    for (unsigned i=0;i<APP_ENV_LIMIT;++i) {
        int64_t r=uos_env_entry(i,&e);if (r<0) { return 1; }if (!r) { break; }
        uos_puts(e.name);uos_puts("=");uos_puts(e.value);uos_puts("\n");
    }return 0;
}
