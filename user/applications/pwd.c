#include "user.h"
int app_main(void)
{ char path[256];if (uos_getcwd(path,sizeof(path))<0) { return 1; }uos_puts(path);uos_puts("\n");return 0; }
