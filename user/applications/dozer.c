#include "user.h"
int app_main(void)
{ for (;;) { if (uos_sleep_ms(1000)) { return 1; } } }
