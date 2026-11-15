#include "user.h"
int app_main(void)
{
    unsigned char bytes[512];for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    for (unsigned i=0;i<16;++i) { if (uos_write(bytes,512)!=512) { return 1; } }return 0;
}
