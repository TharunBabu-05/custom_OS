#include "dir_walk.h"

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }

static bool matches(const char *name,const char *pattern)
{
    const char *star=0,*retry=0;
    while (*name) {
        if (*pattern=='*') {
            star=++pattern;
            retry=name;
        } else if (*pattern=='?' || *pattern==*name) {
            ++pattern;++name;
        } else {
            if (!star) { return false; }
            pattern=star;
            name=++retry;
        }
    }
    while (*pattern=='*') { ++pattern; }
    return !*pattern;
}

static bool show(const char *path,const char *name,const struct uos_entry *entry,void *context)
{
    (void)entry;
    const char *pattern=context;
    if (pattern && !matches(name,pattern)) { return true; }
    size_t bytes=uos_length(path);
    return uos_write(path,bytes)==(int64_t)bytes && uos_write("\n",1)==1;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char path[DIR_WALK_PATH_BYTES]=".",option[8],pattern[128];
    if (argc>4) { goto usage; }
    if ((argc==2 || argc==4) && uos_arg(1,path,sizeof(path))<=0) { goto usage; }
    if (argc>=3 && (uos_arg((unsigned)argc-2,option,sizeof(option))<=0 || !same(option,"-name") ||
                   uos_arg((unsigned)argc-1,pattern,sizeof(pattern))<=0)) {
        goto usage;
    }
    enum dir_walk_result result=dir_walk(path,show,argc>=3 ? pattern : 0);
    if (result==DIR_WALK_OK) { return 0; }
    if (result!=DIR_WALK_STOP) { uos_puts("find: ");uos_puts(dir_walk_error(result)); }
    return 1;
usage:
    uos_puts("Usage: find [path] [-name pattern]\n");return 2;
}
