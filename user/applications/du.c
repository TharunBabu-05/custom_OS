#include "dir_walk.h"

struct disk_usage { uint64_t bytes,files,directories; };

static bool count(const char *path,const char *name,const struct uos_entry *entry,void *context)
{
    (void)path;(void)name;
    struct disk_usage *usage=context;
    if (entry->type==UOS_DIRECTORY) { ++usage->directories; }
    else {
        ++usage->files;
        usage->bytes+=entry->bytes;
    }
    return true;
}

static bool number(uint64_t value)
{
    char digits[20];
    size_t at=sizeof(digits);
    do { digits[--at]=(char)('0'+value%10);value/=10; } while (value);
    return uos_write(digits+at,sizeof(digits)-at)==(int64_t)(sizeof(digits)-at);
}

int app_main(void)
{
    char path[DIR_WALK_PATH_BYTES]=".";
    int64_t argc=uos_argc();
    if (argc>2 || (argc==2 && uos_arg(1,path,sizeof(path))<=0)) {
        uos_puts("Usage: du [path]\n");return 2;
    }
    struct disk_usage usage={0};
    enum dir_walk_result result=dir_walk(path,count,&usage);
    if (result!=DIR_WALK_OK) { uos_puts("du: ");uos_puts(dir_walk_error(result));return 1; }
    return uos_puts("bytes=")==6 && number(usage.bytes) &&
        uos_puts(" files=")==7 && number(usage.files) &&
        uos_puts(" directories=")==13 && number(usage.directories) && uos_puts("\n")==1 ? 0 : 1;
}
