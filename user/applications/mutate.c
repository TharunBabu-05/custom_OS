#include "user.h"
int app_main(void)
{
    const char *dir="/tmp/.user-fs",*file="/tmp/.user-fs/note";struct uos_entry e;char b[8];
    if (uos_stat(dir,&e)!=UOS_ERROR) { uos_puts("mutation test needs absent /tmp/.user-fs\n");return 1; }
    if (uos_mkdir(dir) || uos_replace(file,"abcdef",6)!=6) { return 2; }
    int64_t h=uos_open(file,UOS_READ);if (h<0) { return 3; }
    if (uos_replace(file,"x",1)!=UOS_ERROR || uos_remove(file,0)!=UOS_ERROR || uos_read((uint64_t)h,b,6)!=6 || b[0]!='a' || uos_close((uint64_t)h)) { return 4; }
    if (uos_replace(file,(const void *)0,1)!=UOS_EFAULT || uos_stat(file,&e) || e.bytes!=6) { return 5; }
    if (uos_replace(file,"x",1)!=1 || uos_stat(file,&e) || e.bytes!=1) { return 6; }
    if (uos_replace(file,"",0)!=0 || uos_stat(file,&e) || e.bytes) { return 7; }
    if (uos_call(UOS_SYS_MKDIR,(uintptr_t)"a\0b",3,0,0,0,0)!=UOS_ERROR) { return 8; }
    if (uos_call(UOS_SYS_MKDIR,0,3,0,0,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_REMOVE,(uintptr_t)file,uos_length(file),2,0,0,0)!=UOS_ERROR ||
        uos_call(UOS_SYS_REPLACE,(uintptr_t)file,uos_length(file),(uintptr_t)b,513,0,0)!=UOS_ERROR) { return 9; }
    if (uos_replace("/boot/CONFIG.TXT","broken",6)!=UOS_ERROR || uos_mkdir("/boot/new")!=UOS_ERROR || uos_remove("/boot/CONFIG.TXT",0)!=UOS_ERROR) { return 10; }
    if (uos_chdir(dir) || uos_remove(".",1)!=UOS_ERROR || uos_remove("..",1)!=UOS_ERROR || uos_chdir("/")) { return 11; }
    if (uos_remove(file,0) || uos_remove(dir,1)) { return 12; }
    uos_puts("PASS: EL0 mutation bounds, atomic replacement, leases, read-only boot and cwd protection.\n");return 0;
}
