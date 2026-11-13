#include "user.h"
static void print_entry(const char *name,const struct uos_entry *e)
{
    char digits[20];size_t n=0;uint64_t size=e->bytes;
    uos_puts(e->type==UOS_DIRECTORY ? "d " : "f ");
    if (e->type==UOS_FILE) {
        do { digits[n++]=(char)('0'+size%10);size/=10; } while (size);
        while (n) { uos_write(&digits[--n],1); }uos_puts(" ");
    }
    uos_puts(name);uos_puts(e->type==UOS_DIRECTORY ? "/\n" : "\n");
}
int app_main(void)
{
    char path[256];struct uos_entry entry;int64_t argc=uos_argc();path[0]='.';path[1]=0;
    if (argc>2 || (argc==2 && uos_arg(1,path,sizeof(path))<=0)) { uos_puts("usage: LS.ELF [path]\n");return 2; }
    if (uos_stat(path,&entry)<0) { uos_puts("ls: path unavailable\n");return 1; }
    if (entry.type==UOS_FILE) { print_entry(path,&entry);return 0; }
    for (unsigned i=0;i<1024;++i) {
        int64_t r=uos_readdir(path,i,&entry);if (r<0) { uos_puts("ls: directory read failed\n");return 1; }
        if (!r) { return 0; }print_entry(entry.name,&entry);
    }
    uos_puts("ls: directory limit reached\n");return 1;
}
