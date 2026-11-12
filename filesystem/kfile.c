/* Bounded owned file descriptions. VFS mutation leases preserve path identity
 * while files are open; richer inode/unlink and preemptive I/O come later. */
#include "kfile.h"
#include "vfs.h"
#include "console.h"
struct description {
    uint64_t owner,token;
    uint32_t offset,length;
    unsigned flags;
    char path[RAMFS_PATH_MAX];
};
static struct description files[KFILE_LIMIT];
static uint64_t generation=1;
static bool operating;
static unsigned char scratch[RAMFS_FILE_MAX];
static void wipe(void *p,size_t n) { volatile unsigned char *out=p;while (n--) { *out++=0; } }
static void copy(void *d,const void *s,size_t n) { unsigned char *out=d;const unsigned char *in=s;while (n--) { *out++=*in++; } }
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static struct description *lookup(uint64_t owner,uint64_t token)
{
    if (!owner || !token) { return NULL; }
    struct description *f=&files[token&15];return f->owner==owner && f->token==token ? f : NULL;
}
static bool conflict(const char *path,bool writer)
{
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        if (files[i].token && same(path,files[i].path) && (writer || (files[i].flags&KFILE_WRITE))) { return true; }
    }return false;
}
bool kfile_path_busy(const char *path,bool directory)
{
    if (!path) { return false; }
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        if (!files[i].token) { continue; }
        if (same(path,files[i].path)) { return true; }
        if (directory) {
            if (path[0]=='/' && !path[1]) { return true; }
            size_t n=0;while (path[n] && path[n]==files[i].path[n]) { ++n; }
            if (!path[n] && files[i].path[n]=='/') { return true; }
        }
    }return false;
}
enum fs_result kfile_open(uint64_t owner,const char *cwd,const char *path,unsigned flags,uint64_t *handle)
{
    if (handle) { *handle=0; }
    if (!owner || !cwd || !path || !handle || !(flags&(KFILE_READ|KFILE_WRITE)) ||
        (flags&~(KFILE_READ|KFILE_WRITE|KFILE_CREATE|KFILE_TRUNC)) ||
        ((flags&(KFILE_CREATE|KFILE_TRUNC)) && !(flags&KFILE_WRITE))) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }operating=true;
    enum fs_result r=FS_NO_HANDLES;unsigned slot=KFILE_LIMIT;
    for (unsigned i=0;i<KFILE_LIMIT;++i) { if (!files[i].token) { slot=i;break; } }
    if (slot==KFILE_LIMIT || !generation) { goto out; }
    char absolute[RAMFS_PATH_MAX];bool directory=false,missing=false;
    r=vfs_resolve(cwd,path,absolute,&directory);
    if (r==FS_NOT_FOUND && (flags&KFILE_CREATE)) {
        size_t n=0;while (n<RAMFS_PATH_MAX && path[n]) { ++n; }
        if (!n || n==RAMFS_PATH_MAX) { r=FS_INVALID_PATH;goto out; }
        if (path[n-1]=='/') { r=FS_NOT_DIR;goto out; }
        r=vfs_prepare_write(cwd,path,absolute);missing=r==FS_OK;
    }
    if (r!=FS_OK) { goto out; }
    if (!missing && directory) { r=FS_IS_DIR;goto out; }
    if ((flags&KFILE_WRITE) && vfs_read_only(absolute)) { r=FS_READ_ONLY;goto out; }
    if (conflict(absolute,(flags&KFILE_WRITE)!=0)) { r=FS_BUSY;goto out; }
    uint32_t bytes=0;
    if (!missing) { r=vfs_file_size(absolute,&bytes);if (r!=FS_OK) { goto out; } }
    if (bytes>65536 || ((flags&KFILE_WRITE) && bytes>RAMFS_FILE_MAX)) { r=FS_FILE_TOO_LARGE;goto out; }
    if (missing || (flags&KFILE_TRUNC)) {
        r=vfs_replace_open(absolute,"",0);if (r!=FS_OK) { goto out; }bytes=0;
    }
    struct description *f=&files[slot];
    f->owner=owner;f->token=(generation<<4)|slot;f->offset=0;f->length=bytes;f->flags=flags;
    size_t n=0;do { f->path[n]=absolute[n]; } while (absolute[n++]);
    generation=generation==(UINT64_MAX>>4) ? 0 : generation+1;
    *handle=f->token;r=FS_OK;
out:
    operating=false;return r;
}
enum fs_result kfile_read(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,size_t *count)
{
    if (count) { *count=0; }if (!count || (!buffer && capacity)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct description *f=lookup(owner,handle);if (!f) { return FS_BAD_HANDLE; }
    if (!(f->flags&KFILE_READ)) { return FS_ACCESS; }
    if (!capacity || f->offset==f->length) { return FS_OK; }
    size_t n=f->length-f->offset;if (n>capacity) { n=capacity; }size_t actual=0;
    operating=true;enum fs_result r=vfs_read(f->path,f->offset,buffer,n,&actual);operating=false;
    /* Pinned length and backend contracts require an exact read before EOF.
     * Unexpected shortening must not silently advance the application's cursor. */
    if (r!=FS_OK) { return r; }if (actual!=n) { return FS_IO; }
    f->offset+=(uint32_t)actual;*count=actual;return FS_OK;
}
enum fs_result kfile_write(uint64_t owner,uint64_t handle,const void *buffer,size_t length,size_t *count)
{
    if (count) { *count=0; }if (!count || (!buffer && length)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct description *f=lookup(owner,handle);if (!f) { return FS_BAD_HANDLE; }
    if (!(f->flags&KFILE_WRITE)) { return FS_ACCESS; }if (!length) { return FS_OK; }
    if (length>RAMFS_FILE_MAX || f->offset>RAMFS_FILE_MAX-length) { return FS_FILE_TOO_LARGE; }
    operating=true;size_t actual=0;enum fs_result r=FS_OK;
    if (f->length) {
        r=vfs_read(f->path,0,scratch,f->length,&actual);
        if (r!=FS_OK || actual!=f->length) { if (r==FS_OK) { r=FS_IO; }goto out; }
    }
    copy(scratch+f->offset,buffer,length);
    uint32_t end=f->offset+(uint32_t)length,total=end>f->length ? end : f->length;
    r=vfs_replace_open(f->path,scratch,total);
    if (r==FS_OK) { f->offset=end;f->length=total;*count=length; }
out:
    wipe(scratch,sizeof(scratch));operating=false;return r;
}
enum fs_result kfile_seek(uint64_t owner,uint64_t handle,int64_t delta,enum kfile_whence whence,uint32_t *position)
{
    if (!position || (whence!=KFILE_SET && whence!=KFILE_CUR && whence!=KFILE_END)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct description *f=lookup(owner,handle);if (!f) { return FS_BAD_HANDLE; }
    uint64_t base=whence==KFILE_SET ? 0 : whence==KFILE_CUR ? f->offset : f->length,next;
    if (delta<0) {
        uint64_t magnitude=(uint64_t)(-(delta+1))+1;if (magnitude>base) { return FS_BAD_ARGUMENT; }next=base-magnitude;
    } else {
        if ((uint64_t)delta>f->length-base) { return FS_BAD_ARGUMENT; }next=base+(uint64_t)delta;
    }
    f->offset=(uint32_t)next;*position=f->offset;return FS_OK;
}
enum fs_result kfile_close(uint64_t owner,uint64_t handle)
{
    if (operating) { return FS_BUSY; }struct description *f=lookup(owner,handle);if (!f) { return FS_BAD_HANDLE; }
    wipe(f,sizeof(*f));return FS_OK;
}
unsigned kfile_close_owner(uint64_t owner)
{
    if (!owner || operating) { return 0; }unsigned n=0;
    for (unsigned i=0;i<KFILE_LIMIT;++i) { if (files[i].token && files[i].owner==owner) { wipe(&files[i],sizeof(files[i]));++n; } }return n;
}
unsigned kfile_count(void)
{ unsigned n=0;for (unsigned i=0;i<KFILE_LIMIT;++i) { if (files[i].token) { ++n; } }return n; }
void kfile_info(void)
{
    console_write("Open files: ");console_u64(kfile_count());console_write("/16; shared readers or one writer; task-owned kernel handles\n");
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        const struct description *f=&files[i];if (!f->token) { continue; }
        console_write("handle=");console_u64(f->token);console_write(" owner=");console_u64(f->owner);console_write(" mode=");
        console_write(f->flags&KFILE_READ ? (f->flags&KFILE_WRITE ? "rw" : "r") : "w");
        console_write(" offset=");console_u64(f->offset);console_write(" bytes=");console_u64(f->length);console_write(" path=");console_write(f->path);console_write("\n");
    }
}
