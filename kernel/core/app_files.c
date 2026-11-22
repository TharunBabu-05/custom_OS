#include "process.h"
#include "elf_app.h"
#include "kfile.h"
#include "vfs.h"
#include "console.h"
#include "sha256.h"

enum { APP_NAME_BYTES=21, APP_VERSION_BYTES=16, APP_RELEASE_SLOTS=3, APP_STORE_PATH=40 };
static const char app_root[]="/data/.apps";
static const char record_magic[]="OUROSAP1";
static const uint64_t install_owner=UINT64_MAX-2;
static unsigned char installed[VFS_FILE_MAX],check_bytes[512];

struct release {
    char version[APP_VERSION_BYTES];
    unsigned char digest[32];
    unsigned char slot;
};
struct app_record {
    char magic[8];
    struct release active,previous;
};
_Static_assert(sizeof(struct app_record)==106,"application record layout");

static bool equal(const void *a,const void *b,size_t length)
{
    const unsigned char *p=a,*q=b;
    for (size_t i=0;i<length;++i) { if (p[i]!=q[i]) { return false; } }
    return true;
}
static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static size_t length(const char *s)
{ size_t n=0;while (s[n]) { ++n; }return n; }
static char *append(char *out,const char *text)
{ while (*text) { *out++=*text++; }*out=0;return out; }

static void wipe(void)
{
    volatile unsigned char *p=installed;
    for (size_t i=0;i<sizeof(installed);++i) { p[i]=0; }
    p=check_bytes;
    for (size_t i=0;i<sizeof(check_bytes);++i) { p[i]=0; }
}
static enum fs_result read_file(const char *path,void *out,size_t bytes)
{
    uint64_t handle;
    enum fs_result result=kfile_open(install_owner,"/",path,KFILE_READ,&handle);
    if (result!=FS_OK) { return result; }
    size_t received=0;
    result=kfile_read(install_owner,handle,out,bytes,&received);
    enum fs_result closed=kfile_close(install_owner,handle);
    if (result!=FS_OK) { return result; }
    if (closed!=FS_OK) { return closed; }
    return received==bytes ? FS_OK : FS_IO;
}
static enum fs_result load_elf(const char *path,size_t limit,uint32_t *bytes,enum app_elf_result *error)
{
    enum fs_result result=vfs_file_size(path,bytes);
    if (result!=FS_OK) { return result; }
    if (!*bytes || *bytes>sizeof(installed) || *bytes>limit) { return FS_FILE_TOO_LARGE; }
    result=read_file(path,installed,*bytes);
    if (result!=FS_OK) { return result; }
    struct app_image_plan plan;
    *error=app_elf_plan(installed,*bytes,&plan);
    return *error==APP_ELF_OK ? FS_OK : FS_UNSUPPORTED;
}
static enum fs_result write_elf(const char *path,uint32_t bytes)
{
    enum fs_result result=vfs_write(path,installed,bytes);
    if (result==FS_OK) { result=vfs_data_sync(); }
    if (result!=FS_OK) { return result; }
    uint64_t handle;
    result=kfile_open(install_owner,"/",path,KFILE_READ,&handle);
    if (result!=FS_OK) { return result; }
    for (unsigned offset=0;offset<bytes;) {
        size_t wanted=bytes-offset,received=0;
        if (wanted>sizeof(check_bytes)) { wanted=sizeof(check_bytes); }
        result=kfile_read(install_owner,handle,check_bytes,wanted,&received);
        if (result!=FS_OK) { break; }
        if (received!=wanted || !equal(check_bytes,installed+offset,wanted)) { result=FS_IO;break; }
        offset+=(unsigned)wanted;
    }
    enum fs_result closed=kfile_close(install_owner,handle);
    return result==FS_OK ? closed : result;
}
static enum fs_result source_path(const char *source,char *path)
{
    bool directory;
    enum fs_result result=vfs_resolve(filesystem_cwd(),source,path,&directory);
    return result==FS_OK && directory ? FS_IS_DIR : result;
}
static enum fs_result install(const char *source,const char *target,bool update,enum app_elf_result *error)
{
    char from[RAMFS_PATH_MAX],to[RAMFS_PATH_MAX];
    enum fs_result result=source_path(source,from);
    if (result!=FS_OK) { return result; }
    size_t target_bytes=length(target);
    if (!target_bytes || target[target_bytes-1]=='/') { return FS_INVALID_PATH; }
    bool directory;
    result=vfs_resolve(filesystem_cwd(),target,to,&directory);
    if (result==FS_OK && (directory || !update)) { return directory ? FS_IS_DIR : FS_EXISTS; }
    if (result!=(update ? FS_OK : FS_NOT_FOUND)) { return result; }
    result=vfs_prepare_write(filesystem_cwd(),target,to);
    if (result!=FS_OK) { return result; }
    if (length(to)<=6 || !equal(to,"/data/",6) || same(from,to)) { return FS_INVALID_PATH; }
    uint32_t bytes;
    result=load_elf(from,vfs_write_limit(to),&bytes,error);
    if (result==FS_OK) { result=write_elf(to,bytes); }
    if (result==FS_OK) {
        console_write(update ? "Updated validated ELF: " : "Installed validated ELF: ");
        console_write(to);console_write(" bytes=");console_u64(bytes);
        console_write("; synchronous commit and full readback verified.\n");
    }
    return result;
}

static bool label(const char *s,unsigned capacity,bool version)
{
    for (unsigned i=0;i<capacity;++i) {
        char c=s[i];
        if (!c) { return i!=0; }
        bool alnum=(c>='a' && c<='z') || (c>='0' && c<='9') || (version && c>='A' && c<='Z');
        if (!alnum && !(i && (c=='-' || c=='_' || (version && (c=='.' || c=='+'))))) { return false; }
    }
    return false;
}
static void app_path(char path[APP_STORE_PATH],const char *name,unsigned slot)
{
    char *end=append(path,app_root);
    *end++='/';end=append(end,name);
    if (!slot) { append(end,".app"); }
    else { *end++='.';*end++=(char)('0'+slot);append(end,".elf"); }
}
static void digest_image(uint32_t bytes,unsigned char digest[32])
{
    struct sha256 hash;
    sha256_init(&hash);sha256_update(&hash,installed,bytes);sha256_final(&hash,digest);
}
static bool valid_release(const struct release *entry)
{
    const struct release empty={0};
    if (!entry->slot) { return equal(entry,&empty,sizeof(empty)); }
    if (entry->slot>APP_RELEASE_SLOTS || !label(entry->version,APP_VERSION_BYTES,true)) { return false; }
    for (size_t i=length(entry->version)+1;i<sizeof(entry->version);++i) {
        if (entry->version[i]) { return false; }
    }
    return true;
}
static enum fs_result load_record(const char *name,struct app_record *record)
{
    char path[APP_STORE_PATH];app_path(path,name,0);
    uint32_t bytes;
    enum fs_result result=vfs_file_size(path,&bytes);
    if (result!=FS_OK) { return result; }
    if (bytes!=sizeof(*record)) { return FS_CORRUPT; }
    result=read_file(path,record,sizeof(*record));
    if (result!=FS_OK) { return result; }
    if (!equal(record->magic,record_magic,sizeof(record->magic)) ||
        !record->active.slot || !valid_release(&record->active) ||
        !valid_release(&record->previous)) { return FS_CORRUPT; }
    if (record->previous.slot && (record->previous.slot==record->active.slot ||
        same(record->active.version,record->previous.version))) { return FS_CORRUPT; }
    return FS_OK;
}
static enum fs_result save_record(const char *name,const struct app_record *record)
{
    char path[APP_STORE_PATH];app_path(path,name,0);
    return vfs_write(path,record,sizeof(*record));
}
static enum fs_result verify_release(const char *path,const struct release *entry,enum app_elf_result *error)
{
    unsigned char digest[32];
    uint32_t bytes;
    enum fs_result result=load_elf(path,sizeof(installed),&bytes,error);
    if (result!=FS_OK) { return result; }
    digest_image(bytes,digest);
    return equal(digest,entry->digest,sizeof(digest)) ? FS_OK : FS_CORRUPT;
}
static void print_app(const char *name,const struct app_record *record)
{
    console_write(name);
    console_write(" active=");console_write(record->active.version);
    console_write(" previous=");console_write(record->previous.slot ? record->previous.version : "none");
    console_write("\n");
}
static enum fs_result deploy(const char *name,const char *version,const char *source,bool upgrade,enum app_elf_result *error)
{
    struct app_record record={.magic="OUROSAP1"};
    enum fs_result result=load_record(name,&record);
    if (upgrade) {
        if (result!=FS_OK) { return result; }
        if (same(version,record.active.version) || same(version,record.previous.version)) { return FS_EXISTS; }
        char active[APP_STORE_PATH];app_path(active,name,record.active.slot);
        result=verify_release(active,&record.active,error);
        if (result!=FS_OK) { return result; }
    } else {
        if (result==FS_OK) { return FS_EXISTS; }
        if (result!=FS_NOT_FOUND) { return result; }
    }

    struct release next={0};
    append(next.version,version);
    for (unsigned slot=1;slot<=APP_RELEASE_SLOTS;++slot) {
        if (slot!=record.active.slot && slot!=record.previous.slot) { next.slot=(unsigned char)slot;break; }
    }
    char from[RAMFS_PATH_MAX],staged[APP_STORE_PATH];app_path(staged,name,next.slot);
    result=source_path(source,from);
    if (result!=FS_OK) { return result; }
    uint32_t bytes;
    result=load_elf(from,vfs_write_limit(staged),&bytes,error);
    if (result!=FS_OK) { return result; }
    digest_image(bytes,next.digest);
    if (!upgrade) {
        result=vfs_mkdir(app_root);
        if (result!=FS_OK && result!=FS_EXISTS) { return result; }
    }
    result=write_elf(staged,bytes);
    if (result!=FS_OK) { return result; }
    /* The only selection change is this atomic record write. Both revisions
     * named by the old record survive a failed or interrupted stage. */
    record.previous=record.active;
    record.active=next;
    result=save_record(name,&record);
    if (result==FS_OK) { print_app(name,&record); }
    return result;
}
static enum fs_result rollback(const char *name,enum app_elf_result *error)
{
    struct app_record record;
    enum fs_result result=load_record(name,&record);
    if (result!=FS_OK) { return result; }
    if (!record.previous.slot) { return FS_NOT_FOUND; }
    char path[APP_STORE_PATH];app_path(path,name,record.previous.slot);
    result=verify_release(path,&record.previous,error);
    if (result!=FS_OK) { return result; }
    struct release previous=record.previous;
    record.previous=record.active;record.active=previous;
    result=save_record(name,&record);
    if (result==FS_OK) { print_app(name,&record); }
    return result;
}
static enum fs_result uninstall(const char *name)
{
    struct app_record record;
    enum fs_result result=load_record(name,&record);
    if (result!=FS_OK && result!=FS_NOT_FOUND) { return result; }
    bool exists=result==FS_OK;
    char path[APP_STORE_PATH];
    for (unsigned slot=0;slot<=APP_RELEASE_SLOTS;++slot) {
        app_path(path,name,slot);
        if (kfile_path_busy(path,false)) { return FS_BUSY; }
    }
    if (exists) {
        app_path(path,name,0);
        result=vfs_remove(path,false);
        if (result!=FS_OK) { return result; }
    }
    /* Once the record is gone, no version is runnable by name. A retry can
     * reclaim remaining version slots without allocating another file. */
    bool found=exists;
    for (unsigned slot=1;slot<=APP_RELEASE_SLOTS;++slot) {
        app_path(path,name,slot);result=vfs_remove(path,false);
        if (result!=FS_OK && result!=FS_NOT_FOUND) { return result; }
        if (result==FS_OK) { found=true; }
    }
    if (!found) { return FS_NOT_FOUND; }
    console_write("Uninstalled ");console_write(name);console_write("\n");
    return FS_OK;
}
static void list_entry(const char *file,bool directory,size_t bytes,void *context)
{
    (void)bytes;
    size_t n=length(file);
    if (directory || n<=4 || n-4>=APP_NAME_BYTES || !same(file+n-4,".app")) { return; }
    char name[APP_NAME_BYTES];
    for (size_t i=0;i<n-4;++i) { name[i]=file[i]; }
    name[n-4]=0;
    if (!label(name,sizeof(name),false)) { return; }
    *(bool *)context=true;
    struct app_record record;
    if (load_record(name,&record)==FS_OK) { print_app(name,&record); }
    else { console_write(name);console_write(" metadata unreadable\n"); }
}
static enum fs_result list_apps(void)
{
    bool seen=false;
    enum fs_result result=vfs_list(app_root,list_entry,&seen);
    if (result==FS_NOT_FOUND || (result==FS_OK && !seen)) { console_write("No managed apps.\n"); }
    return result==FS_NOT_FOUND ? FS_OK : result;
}
static enum fs_result start_app(const char *name,const struct app_args *requested,enum app_elf_result *error)
{
    struct app_record record;
    enum fs_result result=load_record(name,&record);
    if (result!=FS_OK) { return result; }
    char path[APP_STORE_PATH];app_path(path,name,record.active.slot);
    result=verify_release(path,&record.active,error);
    if (result!=FS_OK) { return result; }
    struct app_args args={.count=requested->count};
    for (unsigned i=0;i<args.count;++i) {
        const char *text=i ? requested->bytes+requested->offset[i] : path;
        size_t bytes=length(text)+1;
        if (bytes>sizeof(args.bytes)-args.used) { return FS_PATH_TOO_LONG; }
        args.offset[i]=args.used;
        append(args.bytes+args.used,text);args.used+=(unsigned)bytes;
    }
    (void)process_exec(&args);
    return FS_OK;
}

bool app_file_command(const char *verb,const char *arguments)
{
    bool raw=same(verb,"appinstall") || same(verb,"appupdate");
    bool upgrade=same(verb,"appupgrade"),add=same(verb,"appadd");
    bool list=same(verb,"applist"),start=same(verb,"appstart");
    bool back=same(verb,"approllback"),remove=same(verb,"appuninstall");
    if (!raw && !upgrade && !add && !list && !start && !back && !remove) { return false; }
    struct app_args args={0};
    while (*arguments==' ' || *arguments=='\t') { ++arguments; }
    if (*arguments && !app_args_parse(arguments,&args)) { goto usage; }
    enum app_elf_result error=APP_ELF_OK;
    enum fs_result result;
    const char *name=args.bytes;
    if (raw) {
        if (args.count!=2) { goto usage; }
        result=install(name,args.bytes+args.offset[1],same(verb,"appupdate"),&error);
    } else {
        if (list ? args.count!=0 : !args.count || !label(name,APP_NAME_BYTES,false)) { goto usage; }
        struct vfs_volume_info volume;
        result=vfs_data_mounted() ? vfs_volume_info("/data",&volume) : FS_NOT_READY;
        if (result==FS_OK && volume.version<3) { result=FS_UNSUPPORTED; }
        if (result!=FS_OK) { goto report; }
        if (add || upgrade) {
            const char *version=args.bytes+args.offset[1];
            if (args.count!=3 || !label(version,APP_VERSION_BYTES,true)) { goto usage; }
            result=deploy(name,version,args.bytes+args.offset[2],upgrade,&error);
        } else if (list) { result=list_apps(); }
        else if (start) { result=start_app(name,&args,&error); }
        else {
            if (args.count!=1) { goto usage; }
            result=back ? rollback(name,&error) : uninstall(name);
        }
    }
report:
    wipe();
    if (result!=FS_OK) {
        console_write("app: ");
        if (error!=APP_ELF_OK) { console_write("installation refused: ");console_write(app_elf_error(error)); }
        else { console_write(ramfs_error(result)); }
        console_write("\n");
    }
    return true;
usage:
    if (raw) { console_write("Usage: appinstall <source ELF> </data/new-name> | appupdate <source ELF> </data/existing-name>\n"); }
    else {
        console_write("Usage: appadd | appupgrade <name> <version> <ELF>; applist; appstart <name> [arguments]\n");
        console_write("approllback | appuninstall <name>\n");
    }
    return true;
}
