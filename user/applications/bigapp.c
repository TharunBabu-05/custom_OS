/* A real RX ELF larger than the former 8 KiB persistent-file limit. */
#include "user.h"
static const unsigned char payload[10000]={[0]=7,[4095]=19,[8192]=73,[9999]=201};
int app_main(void)
{
    const volatile unsigned char *p=payload;uint32_t digest=2166136261u;
    for (unsigned i=0;i<sizeof(payload);++i) { digest=(digest^p[i])*16777619u; }
    if (digest==2665590177u) {
        uos_puts("PASS: installed application reads all 10000 RX bytes beyond the former 8 KiB file limit.\n");return 0;
    }
    return 1;
}
