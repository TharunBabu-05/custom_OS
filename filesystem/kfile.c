/* Bounded owned file descriptions. VFS mutation leases preserve path identity
 * while files are open; richer inode/unlink and preemptive I/O come later. */
#include "kfile.h"
#include "vfs.h"
#include "console.h"
struct description {
    uint32_t offset,length;
    unsigned flags,refs;
    char path[RAMFS_PATH_MAX];
};
struct descriptor {
    uint64_t owner,token;
    struct description *file;
    unsigned flags;
};
static struct descriptor handles[KFILE_LIMIT];
static struct description files[KFILE_LIMIT];
static uint64_t generation=1;
static bool operating;
static unsigned char scratch[VFS_FILE_MAX];
static void wipe(void *p,size_t n) { volatile unsigned char *out=p;while (n--) { *out++=0; } }
static void copy(void *d,const void *s,size_t n) { unsigned char *out=d;const unsigned char *in=s;while (n--) { *out++=*in++; } }
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static struct descriptor *lookup(uint64_t owner,uint64_t token)
{
    if (!owner || !token) { return NULL; }
    struct descriptor *fd=&handles[token&(KFILE_LIMIT-1)];
    return fd->owner==owner && fd->token==token ? fd : NULL;
}
enum { TOKEN_BITS=4, ACCESS=KFILE_READ|KFILE_WRITE };
_Static_assert(KFILE_LIMIT==(1<<TOKEN_BITS),"file token slot encoding");
static unsigned free_slot(void)
{
    for (unsigned i=0;i<KFILE_LIMIT;++i) { if (!handles[i].token) { return i; } }
    return KFILE_LIMIT;
}
static uint64_t install(unsigned slot,uint64_t owner,struct description *file,unsigned flags)
{
    uint64_t token=(generation<<TOKEN_BITS)|slot;
    generation=generation==((uint64_t)INT64_MAX>>TOKEN_BITS) ? 0 : generation+1;
    handles[slot]=(struct descriptor){owner,token,file,flags};
    ++file->refs;
    return token;
}
static void close_descriptor(struct descriptor *fd)
{
    if (!--fd->file->refs) { wipe(fd->file,sizeof(*fd->file)); }
    wipe(fd,sizeof(*fd));
}
static struct kfile_status status_of(const struct descriptor *fd)
{
    const struct description *f=fd->file;
    return (struct kfile_status){f->length,f->offset,(fd->flags&ACCESS)|(f->flags&KFILE_APPEND)};
}
static bool conflict(const char *path,bool writer)
{
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        if (files[i].refs && same(path,files[i].path) && (writer || (files[i].flags&KFILE_WRITE))) { return true; }
    }return false;
}
bool kfile_path_busy(const char *path,bool directory)
{
    if (!path) { return false; }
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        if (!files[i].refs) { continue; }
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
        (flags&~(KFILE_READ|KFILE_WRITE|KFILE_CREATE|KFILE_TRUNC|KFILE_EXCL|KFILE_APPEND|KFILE_CLOEXEC)) ||
        ((flags&(KFILE_CREATE|KFILE_TRUNC|KFILE_APPEND)) && !(flags&KFILE_WRITE)) ||
        ((flags&KFILE_EXCL) && !(flags&KFILE_CREATE))) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }operating=true;
    enum fs_result r=FS_NO_HANDLES;unsigned slot=free_slot();
    if (slot==KFILE_LIMIT || !generation) { goto out; }
    struct description *f=NULL;
    for (unsigned i=0;i<KFILE_LIMIT;++i) { if (!files[i].refs) { f=&files[i];break; } }
    if (!f) { goto out; }
    char absolute[RAMFS_PATH_MAX];bool directory=false,missing=false;
    r=vfs_resolve(cwd,path,absolute,&directory);
    if (r==FS_NOT_FOUND && (flags&KFILE_CREATE)) {
        size_t n=0;while (n<RAMFS_PATH_MAX && path[n]) { ++n; }
        if (!n || n==RAMFS_PATH_MAX) { r=FS_INVALID_PATH;goto out; }
        if (path[n-1]=='/') { r=FS_NOT_DIR;goto out; }
        r=vfs_prepare_write(cwd,path,absolute);missing=r==FS_OK;
    }
    if (r!=FS_OK) { goto out; }
    if (!missing && (flags&KFILE_EXCL)) { r=FS_EXISTS;goto out; }
    if (!missing && directory) { r=FS_IS_DIR;goto out; }
    if ((flags&KFILE_WRITE) && vfs_read_only(absolute)) { r=FS_READ_ONLY;goto out; }
    if (conflict(absolute,(flags&KFILE_WRITE)!=0)) { r=FS_BUSY;goto out; }
    uint32_t bytes=0;
    if (!missing) { r=vfs_file_size(absolute,&bytes);if (r!=FS_OK) { goto out; } }
    if (bytes>65536 || ((flags&KFILE_WRITE) && bytes>vfs_write_limit(absolute))) { r=FS_FILE_TOO_LARGE;goto out; }
    if (missing || (flags&KFILE_TRUNC)) {
        r=vfs_replace_open(absolute,"",0);if (r!=FS_OK) { goto out; }bytes=0;
    }
    f->offset=0;f->length=bytes;
    f->flags=flags&(KFILE_READ|KFILE_WRITE|KFILE_APPEND);
    size_t n=0;do { f->path[n]=absolute[n]; } while (absolute[n++]);
    *handle=install(slot,owner,f,flags&(ACCESS|KFILE_CLOEXEC));r=FS_OK;
out:
    operating=false;return r;
}
static enum fs_result read_at(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,
                              uint64_t offset,bool positioned,size_t *count)
{
    if (count) { *count=0; }if (!count || (!buffer && capacity)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    if (!(fd->flags&KFILE_READ)) { return FS_ACCESS; }
    struct description *f=fd->file;
    if (!positioned) { offset=f->offset; }
    if (!capacity || offset>=f->length) { return FS_OK; }
    size_t n=f->length-(size_t)offset;if (n>capacity) { n=capacity; }size_t actual=0;
    operating=true;enum fs_result r=vfs_read(f->path,(size_t)offset,buffer,n,&actual);operating=false;
    /* Pinned length and backend contracts require an exact read before EOF.
     * Unexpected shortening must not silently advance the application's cursor. */
    if (r!=FS_OK) { return r; }if (actual!=n) { return FS_IO; }
    if (!positioned) { f->offset+=(uint32_t)actual; }
    *count=actual;return FS_OK;
}
enum fs_result kfile_read(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,size_t *count)
{ return read_at(owner,handle,buffer,capacity,0,false,count); }
enum fs_result kfile_pread(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,uint64_t offset,size_t *count)
{ return read_at(owner,handle,buffer,capacity,offset,true,count); }

/* The caller holds the write lease and has checked the final length. Only
 * advance the cached length after the filesystem publishes the new bytes. */
static enum fs_result replace_bytes(struct description *f,uint32_t bytes,uint32_t offset,
                                    const void *buffer,size_t length)
{
    size_t preserve=bytes<f->length ? bytes : f->length;
    operating=true;size_t actual=0;enum fs_result r=FS_OK;
    if (preserve) {
        r=vfs_read(f->path,0,scratch,preserve,&actual);
        if (r!=FS_OK || actual!=preserve) { if (r==FS_OK) { r=FS_IO; }goto out; }
    }
    if (length) { copy(scratch+offset,buffer,length); }
    else { wipe(scratch+preserve,bytes-preserve); }
    r=vfs_replace_open(f->path,scratch,bytes);
    if (r==FS_OK) { f->length=bytes; }
out:
    wipe(scratch,sizeof(scratch));operating=false;return r;
}

static enum fs_result write_at(uint64_t owner,uint64_t handle,const void *buffer,size_t length,
                               uint64_t offset,bool positioned,size_t *count)
{
    if (count) { *count=0; }if (!count || (!buffer && length)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    if (!(fd->flags&KFILE_WRITE)) { return FS_ACCESS; }
    struct description *f=fd->file;
    if (!positioned) { offset=(f->flags&KFILE_APPEND) ? f->length : f->offset; }
    if (offset>f->length) { return FS_BAD_ARGUMENT; }
    if (!length) { return FS_OK; }
    size_t maximum=vfs_write_limit(f->path);
    if (length>maximum || offset>maximum-length) { return FS_FILE_TOO_LARGE; }
    uint32_t end=(uint32_t)(offset+length);
    uint32_t total=end>f->length ? end : f->length;
    enum fs_result r=replace_bytes(f,total,(uint32_t)offset,buffer,length);
    if (r==FS_OK) {
        if (!positioned) { f->offset=end; }
        *count=length;
    }
    return r;
}
enum fs_result kfile_write(uint64_t owner,uint64_t handle,const void *buffer,size_t length,size_t *count)
{ return write_at(owner,handle,buffer,length,0,false,count); }
enum fs_result kfile_pwrite(uint64_t owner,uint64_t handle,const void *buffer,size_t length,uint64_t offset,size_t *count)
{ return write_at(owner,handle,buffer,length,offset,true,count); }

enum fs_result kfile_stat(uint64_t owner,uint64_t handle,struct kfile_status *status)
{
    if (!status) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(owner,handle);
    if (!fd) { return FS_BAD_HANDLE; }
    *status=status_of(fd);return FS_OK;
}

enum fs_result kfile_truncate(uint64_t owner,uint64_t handle,uint64_t bytes)
{
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(owner,handle);
    if (!fd) { return FS_BAD_HANDLE; }
    if (!(fd->flags&KFILE_WRITE)) { return FS_ACCESS; }
    struct description *f=fd->file;
    if (bytes>vfs_write_limit(f->path)) { return FS_FILE_TOO_LARGE; }
    if (bytes==f->length) { return FS_OK; }
    enum fs_result r=replace_bytes(f,(uint32_t)bytes,0,NULL,0);
    if (r==FS_OK && f->offset>f->length) { f->offset=f->length; }
    return r;
}
enum fs_result kfile_seek(uint64_t owner,uint64_t handle,int64_t delta,enum kfile_whence whence,uint32_t *position)
{
    if (!position || (whence!=KFILE_SET && whence!=KFILE_CUR && whence!=KFILE_END)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    struct description *f=fd->file;
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
    if (operating) { return FS_BUSY; }struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    close_descriptor(fd);return FS_OK;
}
enum fs_result kfile_dup_to(uint64_t owner,uint64_t handle,uint64_t recipient,unsigned access,unsigned flags,uint64_t *duplicate)
{
    if (!duplicate) { return FS_BAD_ARGUMENT; }
    *duplicate=0;
    if (!recipient || (access&~ACCESS) || (flags&~KFILE_CLOEXEC)) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    unsigned current=fd->flags&ACCESS;
    if (access&~current) { return FS_ACCESS; }
    unsigned slot=free_slot();
    if (slot==KFILE_LIMIT || !generation) { return FS_NO_HANDLES; }
    *duplicate=install(slot,recipient,fd->file,(access ? access : current)|flags);
    return FS_OK;
}
enum fs_result kfile_dup(uint64_t owner,uint64_t handle,unsigned access,unsigned flags,uint64_t *duplicate)
{ return kfile_dup_to(owner,handle,owner,access,flags,duplicate); }
enum fs_result kfile_receive(uint64_t escrow,uint64_t handle,uint64_t recipient)
{
    if (!recipient) { return FS_BAD_ARGUMENT; }
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(escrow,handle);
    if (!fd) { return FS_BAD_HANDLE; }
    fd->owner=recipient;
    return FS_OK;
}
unsigned kfile_owner_count(uint64_t owner)
{
    unsigned count=0;
    if (owner) { for (unsigned i=0;i<KFILE_LIMIT;++i) { count+=handles[i].owner==owner; } }
    return count;
}
enum fs_result kfile_set_cloexec(uint64_t owner,uint64_t handle,bool enabled)
{
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    fd->flags=(fd->flags&~KFILE_CLOEXEC)|(enabled ? KFILE_CLOEXEC : 0);
    return FS_OK;
}
enum fs_result kfile_set_append(uint64_t owner,uint64_t handle,bool enabled)
{
    if (operating) { return FS_BUSY; }
    struct descriptor *fd=lookup(owner,handle);if (!fd) { return FS_BAD_HANDLE; }
    if (!(fd->flags&KFILE_WRITE)) { return FS_ACCESS; }
    fd->file->flags=(fd->file->flags&~KFILE_APPEND)|(enabled ? KFILE_APPEND : 0);
    return FS_OK;
}
size_t kfile_list_owner(uint64_t owner,uint64_t input,uint64_t output,struct kfile_entry out[KFILE_LIMIT])
{
    size_t n=0;
    if (!owner || !out || operating) { return 0; }
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        const struct descriptor *fd=&handles[i];
        if (!fd->token || fd->owner!=owner || fd->token==input || fd->token==output) { continue; }
        out[n++]=(struct kfile_entry){fd->token,status_of(fd),fd->flags&KFILE_CLOEXEC};
    }
    return n;
}
static unsigned close_owner(uint64_t owner,unsigned required_flags)
{
    if (!owner || operating) { return 0; }unsigned n=0;
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        struct descriptor *fd=&handles[i];
        if (fd->token && fd->owner==owner && (fd->flags&required_flags)==required_flags) { close_descriptor(fd);++n; }
    }
    return n;
}
unsigned kfile_close_owner(uint64_t owner) { return close_owner(owner,0); }
unsigned kfile_close_on_exec(uint64_t owner) { return close_owner(owner,KFILE_CLOEXEC); }
unsigned kfile_close_writers(uint64_t owner) { return close_owner(owner,KFILE_WRITE); }
unsigned kfile_count(void)
{ unsigned n=0;for (unsigned i=0;i<KFILE_LIMIT;++i) { if (handles[i].token) { ++n; } }return n; }
void kfile_info(void)
{
    console_write("Open files: ");console_u64(kfile_count());console_write("/16; shared readers or one writer; task-owned kernel handles\n");
    for (unsigned i=0;i<KFILE_LIMIT;++i) {
        const struct descriptor *fd=&handles[i];if (!fd->token) { continue; }
        const struct description *f=fd->file;
        console_write("handle=");console_u64(fd->token);console_write(" owner=");console_u64(fd->owner);console_write(" mode=");
        console_write(fd->flags&KFILE_READ ? (fd->flags&KFILE_WRITE ? "rw" : "r") : "w");
        if (f->flags&KFILE_APPEND) { console_write(" append"); }
        if (fd->flags&KFILE_CLOEXEC) { console_write(" cloexec"); }
        console_write(" offset=");console_u64(f->offset);console_write(" bytes=");console_u64(f->length);console_write(" path=");console_write(f->path);console_write("\n");
    }
}
