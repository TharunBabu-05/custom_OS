#include "user.h"
int app_main(void)
{
    unsigned char bytes[512];char mode[16];
    if (uos_arg(1,mode,sizeof(mode))<1) { return 1; }
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    if (mode[0]=='b') {
        if (uos_write((void *)0,8)!=UOS_EFAULT || uos_write(bytes,513)!=UOS_ERROR ||
            uos_stdin((void *)0,8)!=UOS_EFAULT || uos_stdin(bytes,513)!=UOS_ERROR ||
            uos_stdin((void *)0x2000000000ULL,8)!=UOS_EFAULT ||
            uos_readline((char *)bytes,32)!=UOS_ENOTTY) { return 2; }
        /* Invalid reads did not advance stdin; invalid writes added nothing. */
        if (uos_stdin(bytes,3)!=3 || bytes[0]!='a' || bytes[1]!='b' || bytes[2]!='c' || uos_stdin(bytes,1)!=0) { return 3; }
        for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
        return uos_write(bytes,512)==512 ? 0 : 4;
    }
    if (mode[0]=='h') { if (uos_sleep_ms(200)) { return 5; }return uos_write(bytes,512)==512 ? 0 : 6; }
    if (mode[0]=='o') {
        for (unsigned i=0;i<8;++i) { if (uos_write(bytes,512)!=512) { return 7; } }
        if (uos_write(bytes,1)!=UOS_ERROR) { return 8; }
        return 0; /* Kernel must turn ignored output failure into exit 74. */
    }
    if (mode[0]=='f') {
        if (uos_write(bytes,512)!=512) { return 9; }
        return *(volatile int *)(uintptr_t)0;
    }
    return 10;
}
