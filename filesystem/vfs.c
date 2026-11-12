#include "vfs.h"
#include "console.h"
#include "persist.h"
#include "kfile.h"
static struct fat32_volume boot;
static struct persist_volume data_volume;
static bool data_attempted;
static enum fs_result data_mount_result;
static const char *data_path(const char *p)
{
    const char *prefix="/data";if (!p) { return NULL; }
    for (unsigned i=0;i<5;++i) { if (p[i]!=prefix[i]) { return NULL; } }
    return !p[5] ? "/" : p[5]=='/' ? p+5 : NULL;
}
enum fs_result vfs_mount_data(const struct block_device *device)
{
    if (data_volume.ready) { return FS_BUSY; }
    if (!data_attempted) { data_attempted=true;data_mount_result=persist_mount(&data_volume,device); }
    return data_mount_result;
}
enum fs_result vfs_data_sync(void) { return persist_sync(&data_volume); }
void vfs_data_info(void)
{
    if (!data_volume.ready) {
        console_write("/data: not mounted; datainit requires the new data-partition image and USB write/flush support.\n");
        if (data_attempted) {
            console_write("Data mount phase=");console_write(data_volume.mount_phase ? data_volume.mount_phase : "arguments");
            console_write(" result=");console_write(ramfs_error(data_mount_result));console_write(" block_result=");console_write(block_error(data_volume.mount_io));
            console_write(" last_read_lba=");console_u64(data_volume.mount_lba);console_write("\nMount failure retained; reboot before retrying. No file writes were accepted.\n");
        }return;
    }
    console_write("/data: OurOS snapshot v1; files=8 max_file_bytes=4096; writes=");console_write(data_volume.failed ? "failed (reboot required)" : "synchronous");
    console_write("\npartition_start_lba=");console_u64(data_volume.start);console_write(" sectors=");console_u64(data_volume.sectors);
    console_write(" bank=");console_u64(data_volume.bank);console_write(" generation=");console_u64(data_volume.generation);console_write(" valid_banks=");console_u64(data_volume.valid_banks);console_write("\n");
}
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
    const char *dp=data_path(path);if (dp) { return persist_stat(&data_volume,dp,directory); }
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
    const char *dp=data_path(absolute);if (dp) { return persist_list(&data_volume,dp,emit,context); }
    const char *path=boot_path(absolute);
    if (path && boot.ready) { struct listing l={emit,context}; return fat32_list(&boot,path,fat_entry,&l); }
    return ramfs_list(0,absolute,emit,context);
}
enum fs_result vfs_read(const char *absolute,uint32_t offset,void *buffer,size_t capacity,size_t *count)
{
    if (!absolute || !buffer || !count) { return FS_BAD_ARGUMENT; }
    const char *dp=data_path(absolute);if (dp) { return persist_read(&data_volume,dp,offset,buffer,capacity,count); }
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
enum fs_result vfs_write(const char *absolute,const void *bytes,size_t length)
{
    char path[RAMFS_PATH_MAX];enum fs_result r=vfs_prepare_write("/",absolute,path);if (r!=FS_OK) { return r; }
    if (kfile_path_busy(path,false)) { return FS_BUSY; }return vfs_replace_open(path,bytes,length);
}
enum fs_result vfs_replace_open(const char *absolute,const void *bytes,size_t length)
{
    if (!absolute || absolute[0]!='/') { return FS_BAD_ARGUMENT; }
    if (vfs_read_only(absolute)) { return FS_READ_ONLY; }const char *dp=data_path(absolute);
    return dp ? persist_write(&data_volume,dp,bytes,length,false) : ramfs_write(0,absolute,bytes,length);
}
enum fs_result vfs_touch(const char *absolute)
{
    char path[RAMFS_PATH_MAX];enum fs_result r=vfs_prepare_write("/",absolute,path);if (r!=FS_OK) { return r; }
    if (kfile_path_busy(path,false)) { return FS_BUSY; }const char *dp=data_path(path);
    return dp ? persist_write(&data_volume,dp,NULL,0,true) : ramfs_touch(0,path);
}
enum fs_result vfs_mkdir(const char *absolute)
{
    char path[RAMFS_PATH_MAX];enum fs_result r=vfs_prepare_write("/",absolute,path);if (r!=FS_OK) { return r; }
    if (kfile_path_busy(path,false)) { return FS_BUSY; }const char *dp=data_path(path);
    return dp ? FS_UNSUPPORTED : ramfs_mkdir(0,path);
}
enum fs_result vfs_remove(const char *absolute,bool directory)
{
    char path[RAMFS_PATH_MAX];bool dir;enum fs_result r=vfs_resolve("/",absolute,path,&dir);if (r!=FS_OK) { return r; }
    if (vfs_read_only(path)) { return FS_READ_ONLY; }
    if (kfile_path_busy(path,directory)) { return FS_BUSY; }const char *dp=data_path(path);
    return dp ? persist_remove(&data_volume,dp,directory) : ramfs_remove(0,path,directory);
}
struct file_size { uint32_t bytes;bool seen,bad; };
static void size_entry(const char *name,bool directory,size_t bytes,void *context)
{
    (void)name;struct file_size *s=context;
    if (s->seen || directory || bytes>UINT32_MAX) { s->bad=true; }s->seen=true;s->bytes=(uint32_t)bytes;
}
enum fs_result vfs_file_size(const char *path,uint32_t *bytes)
{
    if (!path || !bytes) { return FS_BAD_ARGUMENT; }*bytes=0;bool directory;
    enum fs_result r=stat_path(path,&directory);if (r!=FS_OK) { return r; }if (directory) { return FS_IS_DIR; }
    struct file_size size={0};r=vfs_list(path,size_entry,&size);if (r!=FS_OK) { return r; }
    if (!size.seen || size.bad) { return FS_CORRUPT; }*bytes=size.bytes;return FS_OK;
}
