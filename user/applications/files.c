#include "user.h"
int app_main(void)
{
    char bytes[32];int64_t file=uos_open("/etc/hostname",UOS_READ);if (file<0) { return 1; }
    int64_t n=uos_read((uint64_t)file,bytes,sizeof(bytes));
    if (n!=6 || bytes[0]!='o' || bytes[5]!='\n' || uos_close((uint64_t)file)!=0) { return 2; }
    if (uos_write(bytes,(size_t)n)!=n) { return 3; }
    file=uos_open("/tmp/user-note.txt",UOS_WRITE|UOS_CREATE|UOS_TRUNC);if (file<0) { return 4; }
    static const char note[]="hello-from-el0";
    if (uos_write_file((uint64_t)file,note,sizeof(note)-1)!=(int64_t)(sizeof(note)-1) || uos_close((uint64_t)file)!=0) { return 5; }
    file=uos_open("/tmp/user-note.txt",UOS_READ);if (file<0) { return 6; }
    n=uos_read((uint64_t)file,bytes,sizeof(bytes));
    if (n!=(int64_t)(sizeof(note)-1) || bytes[0]!='h' || bytes[n-1]!='0' ||
        uos_seek((uint64_t)file,0,UOS_SEEK_SET)!=0 || uos_close((uint64_t)file)!=0) { return 7; }
    (void)uos_puts("PASS: EL0 owned file open/read/write/seek/close.\n");return 0;
}
