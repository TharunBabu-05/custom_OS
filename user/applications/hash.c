#include "user.h"
#include "sha256.h"
int app_main(void)
{
    unsigned char bytes[512],digest[32];char hex[65];const char *digits="0123456789abcdef";struct sha256 hash;uint64_t total=0;
    if (uos_stdin((void *)0,1)!=UOS_EFAULT || uos_stdin(bytes,513)!=UOS_ERROR ||
        uos_stdin((void *)0x2000000000ULL,1)!=UOS_EFAULT || uos_stdin(0,0)!=0) { return 2; }
    sha256_init(&hash);
    for (;;) {
        int64_t n=uos_stdin(bytes,sizeof(bytes));if (n<0 || total+(uint64_t)n>65536) { return 1; }if (!n) { break; }
        total+=(uint64_t)n;sha256_update(&hash,bytes,(size_t)n);
    }
    sha256_final(&hash,digest);
    for (unsigned i=0;i<32;++i) { hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15]; }hex[64]='\n';
    return uos_write(hex,sizeof(hex))==(int64_t)sizeof(hex) ? 0 : 1;
}
