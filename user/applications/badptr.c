#include "user.h"
int app_main(void)
{
    char byte;
    if (uos_write((const void *)(uintptr_t)0x80000,1)!=UOS_EFAULT ||
        uos_write((const void *)(uintptr_t)0,1)!=UOS_EFAULT ||
        uos_write((const void *)(uintptr_t)UINT64_MAX,8)!=UOS_EFAULT) { return 1; }
    int64_t file=uos_open("/etc/hostname",UOS_READ);if (file<0) { return 2; }
    if (uos_read((uint64_t)file,(void *)(uintptr_t)0x80000,1)!=UOS_EFAULT ||
        uos_read((uint64_t)file,(void *)(uintptr_t)0x2000000000ULL,1)!=UOS_EFAULT ||
        uos_read((uint64_t)file,&byte,1)!=1 || byte!='o' || uos_close((uint64_t)file)!=0) { return 3; }
    (void)uos_puts("PASS: EL0 syscall pointers rejected kernel/null/overflow ranges; valid cursor unchanged.\n");return 0;
}
