#include "ramfs.h"
#include "console.h"
#include "vfs.h"
static char cwd[RAMFS_PATH_MAX]="/";
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
            console_write(cwd); console_write("\n");
        } else {
            console_write("rootfs=ramfs (volatile; files disappear after reboot)\n");
            console_write("files="); console_u64(stats.files); console_write(" directories="); console_u64(stats.directories);
            console_write(" data_pages="); console_u64(stats.pages); console_write(" data_bytes="); console_u64(stats.bytes);
            console_write("\nheap_buffers=");console_u64(stats.buffers);console_write(" capacity_bytes=");console_u64(stats.capacity_bytes);console_write(" (small files share pages)");
            console_write("\nlimits: nodes=64 file_bytes=4096 name_bytes=31 path_bytes=255\n");
            vfs_boot_info();
            vfs_data_info();
        }
        return true;
    }
    char path[RAMFS_PATH_MAX]; int argument=word(&p,path,sizeof(path));
    if (argument<0 || (!argument && !same(verb,"ls") && !same(verb,"cd"))) {
        console_write("Usage: ls [path] | cd [path] | cat/mkdir/touch/rm/rmdir <path> | write <path> <text>\n"); return true;
    }
    if (!argument) { path[0]=same(verb,"cd") ? '/' : '.'; path[1]='\0'; }
    spaces(&p);
    char absolute[RAMFS_PATH_MAX];
    bool dir=false;
    bool mutating=same(verb,"write") || same(verb,"mkdir") || same(verb,"touch");
    enum fs_result resolved=mutating ? vfs_prepare_write(cwd,path,absolute) : vfs_resolve(cwd,path,absolute,&dir);
    if (resolved!=FS_OK) { report(resolved); return true; }
    if (same(verb,"write")) {
        size_t path_length=0; while (path[path_length]) { ++path_length; }
        if (path[path_length-1]=='/') { report(FS_NOT_DIR); return true; }
        size_t length=0; while (p[length]) { ++length; }
        report(vfs_write(absolute,p,length)); return true;
    }
    if (*p) { console_write("fs: only one path argument is supported\n"); return true; }
    if (same(verb,"ls")) { report(vfs_list(absolute,entry,NULL));
    } else if (same(verb,"cd")) {
        if (!dir) { report(FS_NOT_DIR); }
        else { size_t i=0; do { cwd[i]=absolute[i]; } while (absolute[i++]); }
    } else if (same(verb,"cat")) {
        unsigned char data[512]; uint32_t offset=0; unsigned char last='\n';
        for (;;) {
            size_t length=0; enum fs_result r=vfs_read(absolute,offset,data,sizeof(data),&length);
            if (r!=FS_OK) { report(r); break; } if (!length) { break; }
            for (size_t i=0;i<length;++i) {
                unsigned char c=data[i];
                console_putc((c>=32 && c<=126) || c=='\n' || c=='\r' || c=='\t' ? (char)c : '.');
            }
            last=data[length-1]; offset+=(uint32_t)length;
        }
        if (last!='\n') { console_write("\n"); }
    } else if (same(verb,"mkdir")) { report(vfs_mkdir(absolute));
    } else if (same(verb,"touch")) {
        size_t n=0; while (path[n]) { ++n; }
        report(path[n-1]=='/' ? FS_NOT_DIR : vfs_touch(absolute));
    } else {
        if (same(absolute,cwd) || same(absolute,"/")) { report(FS_BUSY); }
        else if (vfs_read_only(absolute)) { report(FS_READ_ONLY); }
        else { report(vfs_remove(absolute,same(verb,"rmdir"))); }
    }
    return true;
}
