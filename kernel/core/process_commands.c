#include "process.h"
#include "package.h"
#include "vfs.h"
#include "console.h"
#include "user_abi.h"
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void space(const char **p) { while (**p==' ') { ++*p; } }
static int word(const char **p,char *out,size_t capacity)
{
    space(p);if (!**p) { return 0; }size_t n=0;
    while (**p && **p!=' ') { if (n+1>=capacity) { return -1; }out[n++]=*(*p)++; }out[n]=0;return 1;
}
static void failure(enum fs_result r)
{ console_write("app: ");console_write(ramfs_error(r));console_write("\n"); }
static bool service_option(const char **p,char option)
{
    space(p);
    if ((*p)[0]!='-' || (*p)[1]!=option || ((*p)[2] && (*p)[2]!=' ')) { return false; }
    *p+=2;
    return true;
}
static void service_command(const char *p)
{
    char action[8],name[SERVICE_NAME_MAX+1];
    space(&p);
    if (!*p) { process_service_info(NULL);return; }
    if (word(&p,action,sizeof(action))!=1) { goto usage; }
    if (same(action,"start")) {
        const char *label=NULL,*requires=NULL;
        char dependency[SERVICE_NAME_MAX+1];
        unsigned permissions=UOS_POLICY_ALL;
        if (service_option(&p,'n')) {
            if (word(&p,name,sizeof(name))!=1) { goto usage; }
            label=name;
            if (service_option(&p,'d')) {
                if (word(&p,dependency,sizeof(dependency))!=1) { goto usage; }
                requires=dependency;
            }
        }
        if (service_option(&p,'m')) {
            char mask[3];
            if (word(&p,mask,sizeof(mask))!=1 || !app_args_uint(mask,UOS_POLICY_ALL,&permissions)) { goto usage; }
        }
        struct app_args args;
        if (!app_args_parse(p,&args)) { goto usage; }
        console_write(process_service_start(&args,label,requires,permissions) ? "Service started.\n" : "Service start refused.\n");
        return;
    }
    int have_name=word(&p,name,sizeof(name));
    space(&p);
    if (*p || have_name<0) { goto usage; }
    const char *target=have_name ? name : NULL;
    if (same(action,"list") && !target) { process_service_list();return; }
    if (same(action,"status")) { process_service_info(target);return; }
    if (same(action,"stop")) { process_service_stop(target);process_service_info(target);return; }
    if (same(action,"restart")) {
        console_write(process_service_restart(target) ? "Service restarted.\n" : "Service restart refused.\n");
        return;
    }
usage:
    console_write("Usage: svc | svc list | svc status|stop|restart [NAME]\n"
                  "       svc start [-n NAME [-d DEPENDENCY]] [-m MASK] ELF [arguments]\n");
}
bool process_command(const char *line)
{
    const char *p=line;char verb[16];if (word(&p,verb,sizeof(verb))!=1) { return false; }
    if (same(verb,"svc")) { service_command(p);return true; }
    if (same(verb,"sha256") || same(verb,"runio") || same(verb,"run") || same(verb,"pipe") || same(verb,"pipeio") || same(verb,"status") || same(verb,"exec") || same(verb,"path") ||
        same(verb,"env") || same(verb,"getenv") || same(verb,"setenv") || same(verb,"unsetenv") || same(verb,"profile") ||
        same(verb,"runlim") || same(verb,"policy")) {
        console_write("Shell command: use appshell and wait for user:/$ (timerstart first if stopped).\n");return true;
    }
    if (same(verb,"appshell")) {
        space(&p);if (*p) { console_write("Usage: appshell\n");return true; }
        (void)process_shell();return true;
    }
    if (same(verb,"appterm")) {
        char number[4];unsigned seconds=0;struct app_args args;
        if (word(&p,number,sizeof(number))!=1 || !app_args_uint(number,300,&seconds) || !seconds ||
            !app_args_parse(p,&args)) { goto term_usage; }
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
    if (app_file_command(verb,p) || package_command(verb,p)) { return true; }
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
    console_write("Usage: apprun <ELF path> [additional ELF paths, up to eight total]\n");return true;
}
