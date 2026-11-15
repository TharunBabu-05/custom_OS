#include "user.h"
int app_main(void)
{
    unsigned char bytes[512];char mode[16]="generate";
    if (uos_argc()>1 && uos_arg(1,mode,sizeof(mode))<1) { return 1; }
    if (mode[0]=='e') { return 0; }
    if (mode[0]=='n' && (uos_pipeline(0,0,1,0)!=UOS_ERROR || uos_spawn(0,1)!=UOS_ERROR)) { return 2; }
    if (mode[0]=='d') {
        if (uos_sleep_ms(100)) { return 3; }size_t total=0;
        for (;;) {
            int64_t n=uos_stdin(bytes,37);if (n<0) { return 4; }if (!n) { break; }
            for (int64_t i=0;i<n;++i) { if (bytes[i]!=(unsigned char)(total+(size_t)i)) { return 5; } }total+=(size_t)n;
        }
        return total==16384 ? 0 : 6;
    }
    if (mode[0]=='l' && uos_sleep_ms(100)) { return 7; }
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    if (uos_write(0,1)!=UOS_EFAULT || uos_write(bytes,513)!=UOS_ERROR || uos_write(0,0)!=0) { return 8; }
    unsigned rounds=mode[0]=='g' ? 32 : 1;size_t n=rounds==32 ? 512 : 17;
    for (unsigned i=0;i<rounds;++i) {
        int64_t written=uos_write(bytes,n);if (written==UOS_EPIPE) { return 0; }if (written!=(int64_t)n) { return 9; }
    }
    return 0;
}
