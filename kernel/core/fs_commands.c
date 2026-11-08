#include "ramfs.h"
#include "console.h"
static int cwd;
static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static void spaces(const char **p) { while (**p==' ') { ++*p; } }
static int word(const char **p,char *out,size_t capacity)
{
    spaces(p); if (!**p) { return 0; }
    size_t n=0;
    while (**p && **p!=' ') {
        if (n+1>=capacity) { return -1; } out[n++]=*(*p)++;
    }
    out[n]='\0'; return 1;
}
static void report(enum fs_result result)
{
    if (result!=FS_OK) { console_write("fs: "); console_write(ramfs_error(result)); console_write("\n"); }
}
static void entry(const char *name,bool directory,size_t bytes,void *context)
{
    (void)context;
    console_write(directory ? "d " : "f ");
    if (!directory) { console_u64(bytes); console_write(" "); }
    console_write(name); if (directory) { console_write("/"); } console_write("\n");
}
bool filesystem_command(const char *line)
{
    const char *p=line; char verb[16];
    if (word(&p,verb,sizeof(verb))!=1) { return false; }
    if (!same(verb,"pwd") && !same(verb,"ls") && !same(verb,"cd") && !same(verb,"cat") &&
        !same(verb,"mkdir") && !same(verb,"touch") && !same(verb,"write") && !same(verb,"rm") &&
        !same(verb,"rmdir") && !same(verb,"fsinfo")) { return false; }
    struct ramfs_stats stats=ramfs_get_stats();
    if (!stats.ready) { report(FS_NOT_READY); return true; }
    spaces(&p);
    if (same(verb,"pwd") || same(verb,"fsinfo")) {
        if (*p) { console_write("Usage: pwd | fsinfo (no arguments)\n"); return true; }
        if (same(verb,"pwd")) {
            char path[RAMFS_PATH_MAX]; enum fs_result r=ramfs_pwd(cwd,path,sizeof(path));
            if (r==FS_OK) { console_write(path); console_write("\n"); } else { report(r); }
        } else {
            console_write("rootfs=ramfs (volatile; files disappear after reboot)\n");
            console_write("files="); console_u64(stats.files); console_write(" directories="); console_u64(stats.directories);
            console_write(" data_pages="); console_u64(stats.pages); console_write(" data_bytes="); console_u64(stats.bytes);
            console_write("\nlimits: nodes=64 file_bytes=4096 name_bytes=31 path_bytes=255\n");
        }
        return true;
    }
    char path[RAMFS_PATH_MAX]; int argument=word(&p,path,sizeof(path));
    if (argument<0 || (!argument && !same(verb,"ls") && !same(verb,"cd"))) {
        console_write("Usage: ls [path] | cd [path] | cat/mkdir/touch/rm/rmdir <path> | write <path> <text>\n"); return true;
    }
    if (!argument) { path[0]=same(verb,"cd") ? '/' : '.'; path[1]='\0'; }
    spaces(&p);
    if (same(verb,"write")) {
        size_t length=0; while (p[length]) { ++length; }
        report(ramfs_write(cwd,path,p,length)); return true;
    }
    if (*p) { console_write("fs: only one path argument is supported\n"); return true; }
    if (same(verb,"ls")) { report(ramfs_list(cwd,path,entry,NULL));
    } else if (same(verb,"cd")) { report(ramfs_chdir(cwd,path,&cwd));
    } else if (same(verb,"cat")) {
        const char *data; size_t length; enum fs_result r=ramfs_read(cwd,path,&data,&length);
        if (r!=FS_OK) { report(r); }
        else {
            for (size_t i=0;i<length;++i) { console_putc(data[i]); }
            if (length && data[length-1]!='\n') { console_write("\n"); }
        }
    } else if (same(verb,"mkdir")) { report(ramfs_mkdir(cwd,path));
    } else if (same(verb,"touch")) { report(ramfs_touch(cwd,path));
    } else { report(ramfs_remove(cwd,path,same(verb,"rmdir"))); }
    return true;
}
