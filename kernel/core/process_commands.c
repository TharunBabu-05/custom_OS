#include "process.h"
#include "elf_app.h"
#include "kfile.h"
#include "vfs.h"
#include "console.h"
static unsigned char installed[RAMFS_FILE_MAX],check_bytes[512];
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void space(const char **p) { while (**p==' ') { ++*p; } }
static int word(const char **p,char *out,size_t capacity)
{
    space(p);if (!**p) { return 0; }size_t n=0;
    while (**p && **p!=' ') { if (n+1>=capacity) { return -1; }out[n++]=*(*p)++; }out[n]=0;return 1;
}
static void wipe(void)
{ volatile unsigned char *p=installed;for (unsigned i=0;i<sizeof(installed);++i) { p[i]=0; }p=check_bytes;for (unsigned i=0;i<sizeof(check_bytes);++i) { p[i]=0; } }
static void failure(enum fs_result r)
{ console_write("app: ");console_write(ramfs_error(r));console_write("\n"); }
static bool flat_data(const char *p)
{
    const char *prefix="/data/";unsigned i=0;while (prefix[i]) { if (p[i]!=prefix[i]) { return false; }++i; }
    if (!p[i]) { return false; }while (p[i]) { if (p[i++]=='/') { return false; } }return true;
}
static void install(const char *source,const char *target)
{
    char src[RAMFS_PATH_MAX],dst[RAMFS_PATH_MAX];bool directory;uint32_t bytes=0;uint64_t handle=0;
    const uint64_t owner=UINT64_MAX-2;enum fs_result r=vfs_resolve(filesystem_cwd(),source,src,&directory);
    if (r!=FS_OK || directory) { failure(r==FS_OK ? FS_IS_DIR : r);return; }
    size_t n=0;while (target[n]) { ++n; }if (!n || target[n-1]=='/') { failure(FS_INVALID_PATH);return; }
    r=vfs_resolve(filesystem_cwd(),target,dst,&directory);
    if (r==FS_OK) { failure(directory ? FS_IS_DIR : FS_EXISTS);return; }
    if (r!=FS_NOT_FOUND) { failure(r);return; }
    r=vfs_prepare_write(filesystem_cwd(),target,dst);
    if (r!=FS_OK) { failure(r);return; }
    if (!flat_data(dst) || same(src,dst)) { failure(FS_INVALID_PATH);return; }
    r=vfs_file_size(src,&bytes);
    if (r!=FS_OK || !bytes || bytes>sizeof(installed)) { failure(r!=FS_OK ? r : FS_FILE_TOO_LARGE);return; }
    r=kfile_open(owner,"/",src,KFILE_READ,&handle);if (r!=FS_OK) { failure(r);return; }
    size_t received=0;r=kfile_read(owner,handle,installed,bytes,&received);
    enum fs_result closed=kfile_close(owner,handle);handle=0;
    if (r!=FS_OK || closed!=FS_OK || received!=bytes) { failure(r!=FS_OK ? r : closed!=FS_OK ? closed : FS_IO);goto done; }
    struct app_image_plan plan;enum app_elf_result er=app_elf_plan(installed,bytes,&plan);
    if (er!=APP_ELF_OK) { console_write("app: installation refused: ");console_write(app_elf_error(er));console_write("\n");goto done; }
    /* No target exists, and no kernel task may run during this foreground
     * operation. Use public VFS writes so all leases and mount bounds apply. */
    r=vfs_write(dst,installed,bytes);if (r==FS_OK) { r=vfs_data_sync(); }
    if (r!=FS_OK) { failure(r);goto done; }
    r=kfile_open(owner,"/",dst,KFILE_READ,&handle);if (r!=FS_OK) { failure(r);goto done; }
    for (unsigned offset=0;offset<bytes;) {
        size_t wanted=bytes-offset;if (wanted>sizeof(check_bytes)) { wanted=sizeof(check_bytes); }
        received=0;r=kfile_read(owner,handle,check_bytes,wanted,&received);
        if (r!=FS_OK || received!=wanted) { failure(r==FS_OK ? FS_IO : r);goto done; }
        for (size_t i=0;i<wanted;++i) { if (check_bytes[i]!=installed[offset+i]) { failure(FS_IO);goto done; } }
        offset+=(unsigned)wanted;
    }
    r=kfile_close(owner,handle);handle=0;if (r!=FS_OK) { failure(r);goto done; }
    console_write("Installed validated ELF: ");console_write(dst);console_write(" bytes=");console_u64(bytes);console_write("; synchronous commit and full readback verified.\n");
done:
    if (handle) { (void)kfile_close(owner,handle); }(void)kfile_close_owner(owner);wipe();
}
bool process_command(const char *line)
{
    const char *p=line;char verb[16];if (word(&p,verb,sizeof(verb))!=1) { return false; }
    if (same(verb,"appshell")) {
        space(&p);if (*p) { console_write("Usage: appshell\n");return true; }
        struct app_args args;(void)app_args_parse("/boot/SHELL.ELF",&args);(void)process_terminal(&args,300);return true;
    }
    if (same(verb,"appterm")) {
        char number[4];unsigned seconds=0;struct app_args args;
        if (word(&p,number,sizeof(number))!=1) { goto term_usage; }
        for (unsigned i=0;number[i];++i) { if (number[i]<'0' || number[i]>'9') { goto term_usage; }seconds=seconds*10+(unsigned)(number[i]-'0'); }
        if (!seconds || seconds>300 || !app_args_parse(p,&args)) { goto term_usage; }
        (void)process_terminal(&args,seconds);return true;
term_usage:console_write("Usage: appterm <1..300 seconds> <ELF> [arguments]\n");return true;
    }
    if (same(verb,"appexec")) {
        struct app_args args;
        if (!app_args_parse(p,&args)) { console_write("Usage: appexec <ELF> [arguments]; max 8 words/511 input bytes, quotes and backslash supported.\n"); }
        else { (void)process_exec(&args); }return true;
    }
    if (same(verb,"tooltest")) {
        space(&p);if (*p) { console_write("Usage: tooltest\n");return true; }
        console_write(process_tools_test() ? "PASS: user arguments, private cwd, metadata, sleep/wake, idle resume and cleanup.\n" : "FAIL: user tools test.\n");return true;
    }
    if (same(verb,"appinfo") || same(verb,"apptest")) {
        space(&p);if (*p) { console_write("Usage: appinfo | apptest\n");return true; }
        if (same(verb,"appinfo")) { process_info(); }
        else { console_write(process_test() ? "PASS: EL0 applications, isolated memory, checked syscalls, preemption, contained faults/timeouts and resource cleanup.\n" : "FAIL: EL0 application test.\n"); }return true;
    }
    if (same(verb,"appinstall")) {
        char source[RAMFS_PATH_MAX],target[RAMFS_PATH_MAX];
        if (word(&p,source,sizeof(source))!=1 || word(&p,target,sizeof(target))!=1) { goto usage; }
        space(&p);if (*p) { goto usage; }install(source,target);return true;
    }
    if (!same(verb,"apprun")) { return false; }
    char paths[PROCESS_LIMIT][RAMFS_PATH_MAX];const char *arguments[PROCESS_LIMIT];unsigned count=0;
    for (;;) {
        space(&p);if (!*p) { break; }if (count==PROCESS_LIMIT) { goto usage; }
        char requested[RAMFS_PATH_MAX];if (word(&p,requested,sizeof(requested))!=1) { goto usage; }
        bool directory;enum fs_result r=vfs_resolve(filesystem_cwd(),requested,paths[count],&directory);
        if (r!=FS_OK || directory) { failure(r==FS_OK ? FS_IS_DIR : r);return true; }
        arguments[count]=paths[count];++count;
    }
    if (!count) { goto usage; }(void)process_run(arguments,count);return true;
usage:
    console_write("Usage: apprun <ELF path> [ELF path] [ELF path] | appinstall <source ELF> </data/new-name>\n");return true;
}
