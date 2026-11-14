#include "user.h"
int app_main(void)
{ char line[8];line[0]='!';if (uos_readline(line,sizeof(line))!=UOS_ENOTTY || line[0]!='!') { return 1; }uos_puts("PASS: noninteractive process cannot consume serial input.\n");return 0; }
