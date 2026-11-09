#include "vfs.h"
#include "console.h"
static struct fat32_volume boot;
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static const char *boot_path(const char *path)
{
    if (path[0]=='/' && path[1]=='b' && path[2]=='o' && path[3]=='o' && path[4]=='t' &&
        (path[5]=='/' || !path[5])) { return path[5] ? path+5 : "/"; } return NULL;
}
enum fs_result vfs_mount_boot(const struct block_device *device)
{
    if (boot.ready) { return FS_BUSY; }
    return fat32_mount(&boot,device);
}
bool vfs_boot_mounted(void) { return boot.ready; }
enum fs_result vfs_boot_open(const char *absolute,struct fat32_file *file)
{
    if (file) { file->ready=false; }
    if (!absolute || !file) { return FS_BAD_ARGUMENT; }
    const char *path=boot_path(absolute);
    if (!boot.ready || !path) { return FS_NOT_READY; }
    return fat32_open(&boot,path,file);
}
bool vfs_read_only(const char *path) { return path && boot_path(path)!=NULL; } /* Reserved mountpoint even before attachment. */
void vfs_boot_info(void)
{
    if (!boot.ready) { console_write("/boot: not mounted; run storageinit\n"); return; }
    console_write("/boot: FAT32 read-only start_lba="); console_u64(boot.start);
    console_write(" sectors="); console_u64(boot.sectors); console_write(" clusters="); console_u64(boot.clusters);
    console_write("\n");
}
static enum fs_result stat_path(const char *path,bool *directory)
{
    const char *relative=boot_path(path);
    if (relative && boot.ready) {
        struct fat32_entry entry; enum fs_result r=fat32_lookup(&boot,relative,&entry);
        if (r==FS_OK) { *directory=entry.directory; } return r;
    }
    int node; enum fs_result r=ramfs_lookup(0,path,&node);
    if (r!=FS_OK) { return r; }
    int ignored; r=ramfs_chdir(0,path,&ignored); *directory=r==FS_OK;
    return r==FS_NOT_DIR ? FS_OK : r;
}
enum fs_result vfs_resolve(const char *cwd,const char *path,char *absolute,bool *directory)
{
    if (!cwd || !path || !absolute || !directory) { return FS_BAD_ARGUMENT; }
    size_t input=0; while (input<RAMFS_PATH_MAX && path[input]) { ++input; }
    if (!input) { return FS_INVALID_PATH; } if (input==RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
    size_t n=0;
    if (*path!='/') {
        while (n<RAMFS_PATH_MAX && cwd[n]) { absolute[n]=cwd[n]; ++n; }
        if (!n || n>=RAMFS_PATH_MAX || absolute[0]!='/') { return FS_INVALID_PATH; }
    } else { absolute[n++]='/'; }
    absolute[n]='\0';
    bool is_dir; enum fs_result r=stat_path(absolute,&is_dir);
    if (r!=FS_OK) { return r; }
    const char *p=path;
    while (*p) {
        if (!is_dir) { return FS_NOT_DIR; }
        while (*p=='/') { ++p; } if (!*p) { break; }
        char name[RAMFS_NAME_MAX+1]; size_t len=0;
        while (*p && *p!='/') { if (len==RAMFS_NAME_MAX) { return FS_NAME_TOO_LONG; } name[len++]=*p++; }
        name[len]='\0';
        if (same(name,".")) { continue; }
        if (same(name,"..")) {
            while (n>1 && absolute[n-1]!='/') { --n; }
            if (n>1) { --n; } absolute[n]='\0';
        } else {
            if (n+(n>1 ? 1 : 0)+len>=RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
            if (n>1) { absolute[n++]='/'; }
            for (size_t i=0;i<len;++i) { absolute[n++]=name[i]; } absolute[n]='\0';
        }
        r=stat_path(absolute,&is_dir); if (r!=FS_OK) { return r; }
    }
    *directory=is_dir; return FS_OK;
}
struct listing { ramfs_emit emit; void *context; };
static bool fat_entry(const struct fat32_entry *entry,void *context)
{
    struct listing *l=context; l->emit(entry->name,entry->directory,entry->size,l->context); return true;
}
enum fs_result vfs_list(const char *absolute,ramfs_emit emit,void *context)
{
    if (!absolute || !emit) { return FS_BAD_ARGUMENT; }
    const char *path=boot_path(absolute);
    if (path && boot.ready) { struct listing l={emit,context}; return fat32_list(&boot,path,fat_entry,&l); }
    return ramfs_list(0,absolute,emit,context);
}
enum fs_result vfs_read(const char *absolute,uint32_t offset,void *buffer,size_t capacity,size_t *count)
{
    if (!absolute || !buffer || !count) { return FS_BAD_ARGUMENT; }
    const char *path=boot_path(absolute);
    if (path && boot.ready) {
        struct fat32_entry entry; enum fs_result r=fat32_lookup(&boot,path,&entry); if (r!=FS_OK) { return r; }
        if (entry.size>65536) { return FS_FILE_TOO_LARGE; }
        return fat32_read(&boot,&entry,offset,buffer,capacity,count);
    }
    const char *data; size_t length; enum fs_result r=ramfs_read(0,absolute,&data,&length); if (r!=FS_OK) { return r; }
    *count=0; if (offset>=length) { return FS_OK; }
    size_t n=length-offset; if (n>capacity) { n=capacity; }
    for (size_t i=0;i<n;++i) { ((char *)buffer)[i]=data[offset+i]; } *count=n; return FS_OK;
}
enum fs_result vfs_prepare_write(const char *cwd,const char *path,char *absolute)
{
    if (!cwd || !path || !absolute) { return FS_BAD_ARGUMENT; }
    bool dir; enum fs_result r=vfs_resolve(cwd,path,absolute,&dir);
    if (r==FS_OK) { return vfs_read_only(absolute) ? FS_READ_ONLY : FS_OK; }
    if (r!=FS_NOT_FOUND) { return r; }
    size_t length=0; while (length<RAMFS_PATH_MAX && path[length]) { ++length; }
    if (!length || length==RAMFS_PATH_MAX) { return FS_INVALID_PATH; }
    while (length>1 && path[length-1]=='/') { --length; }
    size_t start=length; while (start && path[start-1]!='/') { --start; }
    char parent[RAMFS_PATH_MAX];
    if (start) { for (size_t i=0;i<start;++i) { parent[i]=path[i]; } parent[start]='\0'; }
    else { parent[0]='.'; parent[1]='\0'; }
    r=vfs_resolve(cwd,parent,absolute,&dir); if (r!=FS_OK) { return r; } if (!dir) { return FS_NOT_DIR; }
    size_t n=0; while (absolute[n]) { ++n; }
    if (n+(n>1 ? 1 : 0)+(length-start)>=RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
    if (n>1) { absolute[n++]='/'; }
    for (size_t i=start;i<length;++i) { absolute[n++]=path[i]; } absolute[n]='\0';
    return vfs_read_only(absolute) ? FS_READ_ONLY : FS_OK;
}
