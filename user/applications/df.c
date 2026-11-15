#include "user.h"
static void number(uint64_t n)
{ char s[24],out[24];unsigned count=0;do { s[count++]=(char)('0'+n%10);n/=10; }while (n);for (unsigned i=0;i<count;++i) { out[i]=s[count-i-1]; }uos_write(out,count); }
static int show(const char *path)
{
    struct uos_fs_info s;if (uos_statfs(path,&s)) { uos_puts("df: cannot query ");uos_puts(path);uos_puts("\n");return 1; }
    uos_puts(path);uos_puts(s.type==UOS_FS_SNAPSHOT ? " snapshot v" : s.type==UOS_FS_RAM ? " ramfs v" : " fat32 v");number(s.version);
    uos_puts(" max_file_bytes=");number(s.max_write_bytes);uos_puts(" nodes=");number(s.used_nodes);uos_puts("/");number(s.node_limit);
    uos_puts(s.flags&UOS_FS_READONLY ? " read-only\n" : s.flags&UOS_FS_VOLATILE ? " volatile\n" : " persistent\n");return 0;
}
int app_main(void)
{
    char path[256];int result=0;int64_t argc=uos_argc();
    if (argc<2) { result|=show("/");result|=show("/boot");result|=show("/data");return result; }
    for (unsigned i=1;i<(unsigned)argc;++i) { if (uos_arg(i,path,sizeof(path))<0) { return 1; }result|=show(path); }return result;
}
