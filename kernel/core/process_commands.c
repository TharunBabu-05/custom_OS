#include "process.h"
#include "vfs.h"
#include "console.h"
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void space(const char **p) { while (**p==' ') { ++*p; } }
static int word(const char **p,char *out,size_t capacity)
{
    space(p);if (!**p) { return 0; }size_t n=0;
    while (**p && **p!=' ') { if (n+1>=capacity) { return -1; }out[n++]=*(*p)++; }out[n]=0;return 1;
}
static void failure(enum fs_result r)
{ console_write("app: ");console_write(ramfs_error(r));console_write("\n"); }
bool process_command(const char *line)
{
    const char *p=line;char verb[16];if (word(&p,verb,sizeof(verb))!=1) { return false; }
    if (same(verb,"sha256") || same(verb,"runio") || same(verb,"run") || same(verb,"pipe") || same(verb,"pipeio") || same(verb,"status") || same(verb,"exec") || same(verb,"path") ||
        same(verb,"env") || same(verb,"getenv") || same(verb,"setenv") || same(verb,"unsetenv") || same(verb,"profile")) {
        console_write("Shell command: use appshell and wait for user:/$ (timerstart first if stopped).\n");return true;
    }
    if (same(verb,"appshell")) {
        space(&p);if (*p) { console_write("Usage: appshell\n");return true; }
        (void)process_shell();return true;
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
    if (app_file_command(verb,p)) { return true; }
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
    console_write("Usage: apprun <ELF path> [ELF path] [ELF path]\n");return true;
}
