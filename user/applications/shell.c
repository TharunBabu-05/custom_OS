/* Original serial EL0 shell. All filesystem/terminal work crosses SVC;
 * builtins execute in this process, with no kernel command interpreter calls. */
#include "user.h"
#include "app_args.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void number(uint64_t n)
{ char text[20];unsigned at=0;do { text[at++]=(char)('0'+n%10);n/=10; }while (n);while (at) { uos_write(text+--at,1); } }
static int list(const char *path)
{
    struct uos_entry e;if (uos_stat(path,&e)) { return 1; }
    if (e.type==UOS_FILE) { number(e.bytes);uos_puts(" ");uos_puts(path);uos_puts("\n");return 0; }
    for (unsigned i=0;i<1024;++i) {
        int64_t r=uos_readdir(path,i,&e);if (r<0) { return 1; }if (!r) { return 0; }
        uos_puts(e.type==UOS_DIRECTORY ? "d " : "f ");if (e.type==UOS_FILE) { number(e.bytes);uos_puts(" "); }
        uos_puts(e.name);uos_puts(e.type==UOS_DIRECTORY ? "/\n" : "\n");
    }return 1;
}
static int cat(const char *path)
{
    char data[512];int64_t h=uos_open(path,UOS_READ);if (h<0) { return 1; }int result=0;
    for (;;) {
        int64_t n=uos_read((uint64_t)h,data,sizeof(data));if (n<0) { result=1;break; }if (!n) { break; }
        if (uos_write(data,(size_t)n)!=n) { result=1;break; }
    }
    if (uos_close((uint64_t)h)) { result=1; }uos_puts("\n");return result;
}
int app_main(void)
{
    char line[128],cwd[256];struct app_args args;uint64_t last_status=0;
    uos_puts("OurOS EL0 shell. Type help; exit returns to monitor.\n");
    for (;;) {
        if (uos_getcwd(cwd,sizeof(cwd))<0) { return 1; }
        uos_puts("user:");uos_puts(cwd);uos_puts("$ ");
        int64_t n=uos_readline(line,sizeof(line));if (n==UOS_EOF) { return 0; }
        if (n<0) { uos_puts("shell: input rejected; command not executed\n");if (n==UOS_ERROR) { continue; }return 1; }
        if (!n) { continue; }
        if (!app_args_parse(line,&args)) { uos_puts("shell: invalid quoting or too many words\n");continue; }
        const char *cmd=args.bytes,*p=args.count>1 ? args.bytes+args.offset[1] : ".";int result=0;
        if (same(cmd,"exit") && args.count==1) { return 0; }
        if (same(cmd,"help") && args.count==1) {
            uos_puts("help pwd cd ls cat echo write mkdir rm rmdir cp sha256 sync status exit\nrun <ELF> [args]\nrunio <input|-> <new-output|-> <ELF> [args]\nrun waits up to 10s. Ctrl+C stops child or shell. /tmp is volatile.\n");
        } else if (same(cmd,"status") && args.count==1) { number(last_status);uos_puts("\n"); }
        else if ((same(cmd,"run") && args.count>=2) || (same(cmd,"runio") && args.count>=4)) {
            struct app_args child={0};struct uos_stdio io={0};unsigned first=same(cmd,"runio") ? 3 : 1;
            if (first==3) {
                for (unsigned i=0;i<2;++i) {
                    const char *path=args.bytes+args.offset[i+1];char *target=i ? io.output : io.input;
                    if (!same(path,"-")) { do { *target++=*path; }while (*path++); }
                }
            }
            for (unsigned i=first;i<args.count;++i) {
                const char *word=args.bytes+args.offset[i];child.offset[child.count++]=child.used;
                do { child.bytes[child.used++]=*word; }while (*word++);
            }
            int64_t pid=first==3 ? uos_spawn_io(&child,10,&io) : uos_spawn(&child,10),code=pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid);
            if (code<0) { last_status=127;uos_puts("shell: launch/wait failed\n"); }
            else { last_status=(uint64_t)code;uos_puts("child exit=");number(last_status);uos_puts("\n"); }
        }
        else if (same(cmd,"cp") && args.count==3) { result=uos_copy(p,args.bytes+args.offset[2])<0; }
        else if (same(cmd,"sync") && args.count==1) { result=uos_sync()!=0; }
        else if (same(cmd,"sha256") && args.count==2) {
            unsigned char digest[32];char hex[65];const char *digits="0123456789abcdef";
            result=uos_filehash(p,digest)!=0;
            if (!result) { for (unsigned i=0;i<32;++i) { hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15]; }hex[64]=0;uos_puts(hex);uos_puts("  ");uos_puts(p);uos_puts("\n"); }
        }
        else if (same(cmd,"pwd") && args.count==1) { uos_puts(cwd);uos_puts("\n"); }
        else if (same(cmd,"cd") && args.count==2) { result=uos_chdir(p)!=0; }
        else if (same(cmd,"ls") && args.count<=2) { result=list(p); }
        else if (same(cmd,"cat") && args.count==2) { result=cat(p); }
        else if (same(cmd,"echo")) {
            for (unsigned i=1;i<args.count;++i) { if (i>1) { uos_puts(" "); }uos_puts(args.bytes+args.offset[i]); }uos_puts("\n");
        } else if (same(cmd,"write") && args.count==3) {
            const char *data=args.bytes+args.offset[2];size_t bytes=uos_length(data);result=uos_replace(p,data,bytes)!=(int64_t)bytes;
        } else if (same(cmd,"mkdir") && args.count==2) { result=uos_mkdir(p)!=0; }
        else if ((same(cmd,"rm") || same(cmd,"rmdir")) && args.count==2) { result=uos_remove(p,same(cmd,"rmdir"))!=0; }
        else { uos_puts("shell: unknown command or arguments; type help\n");continue; }
        if (result) { uos_puts("shell: operation failed\n"); }
    }
}
