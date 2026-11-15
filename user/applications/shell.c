/* Original serial EL0 shell. All filesystem/terminal work crosses SVC;
 * builtins execute in this process, with no kernel command interpreter calls. */
#include "user.h"
#include "app_args.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void number(uint64_t n)
{ char text[20];unsigned at=0;do { text[at++]=(char)('0'+n%10);n/=10; }while (n);while (at) { uos_write(text+--at,1); } }
static void words(struct app_args *out,const struct app_args *in,unsigned first,unsigned end)
{
    unsigned char *p=(unsigned char *)out;for (size_t i=0;i<sizeof(*out);++i) { p[i]=0; }
    for (unsigned i=first;i<end;++i) {
        const char *word=in->bytes+in->offset[i];out->offset[out->count++]=out->used;
        do { out->bytes[out->used++]=*word; }while (*word++);
    }
}
static int64_t program(struct app_args *args)
{
    const char *name=args->bytes;size_t n=uos_length(name);int extension=0;
    for (size_t i=0;i<n;++i) { if (name[i]=='/') { return 0; }if (name[i]=='.') { extension=1; } }
    const char *dirs[]={"/data/bin/","/boot/"};
    for (unsigned dir=0;dir<2;++dir) { for (unsigned suffix=0;suffix<(extension ? 1u : 2u);++suffix) {
        char path[256];size_t at=0;const char *prefix=dirs[dir];while (*prefix) { path[at++]=*prefix++; }
        if (at+n+(suffix ? 4u : 0u)>=sizeof(path)) { return UOS_ERROR; }
        for (size_t i=0;i<n;++i) { path[at++]=name[i]; }
        if (suffix) { path[at++]='.';path[at++]='e';path[at++]='l';path[at++]='f'; }path[at]=0;
        struct uos_entry entry;if (uos_stat(path,&entry) || entry.type!=UOS_FILE) { continue; }
        struct app_args resolved={0};resolved.count=args->count;resolved.used=(unsigned)at+1;
        for (size_t i=0;i<=at;++i) { resolved.bytes[i]=path[i]; }
        for (unsigned i=1;i<args->count;++i) {
            const char *arg=args->bytes+args->offset[i];size_t bytes=uos_length(arg)+1;
            if (bytes>sizeof(resolved.bytes)-resolved.used) { return UOS_ERROR; }
            resolved.offset[i]=resolved.used;
            for (size_t j=0;j<bytes;++j) { resolved.bytes[resolved.used++]=arg[j]; }
        }
        *args=resolved;return 0;
    } }
    return UOS_ENOENT;
}
static void job_result(int64_t code,int piping,uint64_t *last_status)
{
    if (code<0) { *last_status=127;uos_puts("shell: launch/wait failed; check ELF/input/output paths\n"); }
    else { *last_status=(uint64_t)code;uos_puts(piping ? "pipeline exit=" : "child exit=");number(*last_status);uos_puts("\n"); }
}
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
            uos_puts("help pwd cd ls stat cat echo write mkdir rm rmdir cp mv replace sha256 sync status path exit\nreplace <source> <target>: consumes source, replaces regular target\nrun <program> [args]\nexec <program> [args]: replaces this shell\n");
            uos_puts("runio <input|-> <new-output|-> <program> [args]\npipe <program> [args] -- <program> [args]\npipeio <input|-> <new-output|-> <program> [args] -- <program> [args]\nBare programs search /data/bin then /boot, exact name then .elf.\nrun/pipe: 10s; file I/O jobs: 30s. Ctrl+C cancels.\n");
        } else if (same(cmd,"status") && args.count==1) { number(last_status);uos_puts("\n"); }
        else if (same(cmd,"path") && args.count==1) { uos_puts("/data/bin:/boot\n"); }
        else if (same(cmd,"exec") && args.count>=2) {
            struct app_args next;words(&next,&args,1,args.count);
            int64_t r=program(&next);if (!r) { r=uos_exec(&next); }
            last_status=127;uos_puts(r==UOS_ENOENT ? "shell: program not found\n" : "shell: exec failed; current shell retained\n");
        }
        else if ((same(cmd,"run") && args.count>=2) || (same(cmd,"runio") && args.count>=4) ||
                 (same(cmd,"pipe") && args.count>=4) || (same(cmd,"pipeio") && args.count>=6)) {
            struct app_args child,right;struct uos_stdio io={0};
            bool piping=same(cmd,"pipe") || same(cmd,"pipeio");unsigned first=(same(cmd,"runio") || same(cmd,"pipeio")) ? 3 : 1;
            if (first==3) {
                for (unsigned i=0;i<2;++i) {
                    const char *path=args.bytes+args.offset[i+1];char *target=i ? io.output : io.input;
                    if (!same(path,"-")) { do { *target++=*path; }while (*path++); }
                }
            }
            unsigned split=args.count;
            if (piping) {
                unsigned separators=0;
                for (unsigned i=first;i<args.count;++i) { if (same(args.bytes+args.offset[i],"--")) { split=i;++separators; } }
                if (separators!=1 || split==first || split+1==args.count) { uos_puts("shell: use pipe <ELF> [args] -- <ELF> [args]\n");continue; }
            }
            words(&child,&args,first,split);int64_t code;unsigned seconds=first==3 ? 30 : 10;
            if (program(&child)) { last_status=127;uos_puts("shell: program not found or arguments too long\n");continue; }
            if (piping) {
                words(&right,&args,split+1,args.count);
                if (program(&right)) { last_status=127;uos_puts("shell: program not found or arguments too long\n");continue; }
                code=uos_pipeline(&child,&right,seconds,first==3 ? &io : 0);
            }
            else { int64_t pid=first==3 ? uos_spawn_io(&child,seconds,&io) : uos_spawn(&child,seconds);code=pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid); }
            job_result(code,piping,&last_status);
        }
        else if (same(cmd,"cp") && args.count==3) { result=uos_copy(p,args.bytes+args.offset[2])<0; }
        else if ((same(cmd,"mv") || same(cmd,"replace")) && args.count==3) {
            int64_t r=same(cmd,"replace") ? uos_publish(p,args.bytes+args.offset[2]) : uos_rename(p,args.bytes+args.offset[2]);last_status=r ? 1 : 0;
            if (r) { uos_puts(r==UOS_EEXIST ? "shell: target exists; mv does not overwrite\n" :
                r==UOS_EBUSY ? "shell: file is open; close handles first\n" :
                r==UOS_EXDEV ? "shell: operation requires the same filesystem\n" :
                r==UOS_EROFS ? "shell: read-only filesystem\n" :
                r==UOS_EISDIR ? "shell: operation accepts regular files only\n" :
                r==UOS_ENOENT ? "shell: source or parent not found\n" : "shell: rename failed\n"); }
        }
        else if (same(cmd,"stat") && args.count==2) {
            struct uos_entry e;result=uos_stat(p,&e)!=0;
            if (!result) { uos_puts(e.type==UOS_FILE ? "type=file bytes=" : "type=directory bytes=");number(e.bytes);uos_puts(" path=");uos_puts(p);uos_puts("\n"); }
        }
        else if (same(cmd,"sync") && args.count==1) { result=uos_sync()!=0; }
        else if (same(cmd,"sha256") && args.count==2) {
            unsigned char digest[32];char hex[65];const char *digits="0123456789abcdef";
            int64_t r=uos_filehash(p,digest);result=r!=0;
            if (r==UOS_ENOENT) { uos_puts("shell: file not found: ");uos_puts(p);uos_puts("\n");continue; }
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
        else {
            if (program(&args)) { last_status=127;uos_puts("shell: unknown command or arguments; type help\n");continue; }
            int64_t pid=uos_spawn(&args,10);job_result(pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid),0,&last_status);
        }
        if (result) { uos_puts("shell: operation failed\n"); }
    }
}
