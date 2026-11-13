#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
int app_main(void)
{
    char text[256];struct uos_entry e;
    if (uos_argc()!=4 || uos_arg(1,text,sizeof(text))!=11 || !same(text,"hello world") ||
        uos_arg(2,text,sizeof(text))!=6 || !same(text,"Jack's") || uos_arg(3,text,sizeof(text))!=0) { return 1; }
    text[0]='!';if (uos_arg(4,text,sizeof(text))!=UOS_ERROR || text[0]!='!' ||
        uos_arg(1,text,2)!=UOS_ERROR || text[0]!='!' || uos_arg(1,(char *)0x2000000000ULL,20)!=UOS_EFAULT) { return 2; }
    if (uos_chdir("/etc") || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc")) { return 3; }
    if (uos_chdir("hostname")!=UOS_ERROR || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc")) { return 4; }
    int64_t h=uos_open("hostname",UOS_READ);if (h<0 || uos_read((uint64_t)h,text,6)!=6 || uos_close((uint64_t)h)) { return 5; }
    if (uos_stat("hostname",&e) || e.bytes!=6 || e.type!=UOS_FILE || e.reserved || e.name[0]) { return 6; }
    if (uos_stat(".",&e) || e.type!=UOS_DIRECTORY || e.bytes || e.reserved) { return 7; }
    unsigned found=0;
    for (unsigned i=0;;++i) {
        int64_t n=uos_readdir(".",i,&e);if (n<0 || i>16) { return 8; }if (!n) { break; }
        if (same(e.name,"hostname") && e.bytes==6 && e.type==UOS_FILE) { ++found; }
    }
    if (found!=1 || uos_readdir("hostname",0,&e)!=UOS_ERROR || uos_readdir(".",1024,&e)!=UOS_ERROR) { return 9; }
    if (uos_stat("hostname",(struct uos_entry *)0x2000000000ULL)!=UOS_EFAULT ||
        uos_readdir(".",0,(struct uos_entry *)0)!=UOS_EFAULT || uos_getcwd((char *)0x2000000000ULL,256)!=UOS_EFAULT) { return 10; }
    if (uos_chdir("/") || uos_stat("/etc/hostname/..",&e)!=UOS_ERROR) { return 11; }
    uos_puts("PASS: EL0 argument bounds, private cwd and checked directory metadata.\n");return 0;
}
