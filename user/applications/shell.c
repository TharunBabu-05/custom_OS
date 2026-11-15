/* Original serial EL0 shell. All filesystem/terminal work crosses SVC;
 * builtins execute in this process, with no kernel command interpreter calls. */
#include "user.h"
#include "app_args.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void number(uint64_t n)
{ char text[20];unsigned at=0;do { text[at++]=(char)('0'+n%10);n/=10; }while (n);while (at) { uos_write(text+--at,1); } }
static bool expand_value(const char *name,char *out,size_t capacity,void *context)
{
    if (same(name,"?")) {
        char digits[20];unsigned n=0;uint64_t value=*(const uint64_t *)context;
        do { digits[n++]=(char)('0'+value%10);value/=10; }while (value);if (capacity<=n) { return false; }
        for (unsigned i=0;i<n;++i) { out[i]=digits[n-i-1]; }out[n]=0;return true;
    }
    int64_t r=uos_getenv(name,out,capacity);if (r==UOS_ENOENT) { out[0]=0;return true; }return r>=0;
}
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
    char search[APP_ENV_VALUE_BYTES];if (!n || uos_getenv("PATH",search,sizeof(search))<0) { return UOS_ENOENT; }
    const char *dir=search;
    while (*dir) {
        const char *prefix=dir;size_t span=0;while (*dir && *dir!=':') { ++dir;++span; }if (*dir) { ++dir; }
        /* Empty/relative entries do not implicitly trust the current cwd. */
        if (!span || prefix[0]!='/') { continue; }
        for (unsigned suffix=0;suffix<(extension ? 1u : 2u);++suffix) {
        char path[256];size_t at=0;
        if (span+1+n+(suffix ? 4u : 0u)>=sizeof(path)) { continue; }
        while (at<span) { path[at]=prefix[at];++at; }if (path[at-1]!='/') { path[at++]='/'; }
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
static int environment(const char *key)
{
    if (key) {
        char value[APP_ENV_VALUE_BYTES];if (uos_getenv(key,value,sizeof(value))<0) { return 1; }uos_puts(value);uos_puts("\n");return 0;
    }
    struct app_env_entry entry;
    for (unsigned i=0;i<APP_ENV_LIMIT;++i) {
        int64_t r=uos_env_entry(i,&entry);if (r<0) { return 1; }if (!r) { break; }
        uos_puts(entry.name);uos_puts("=");uos_puts(entry.value);uos_puts("\n");
    }return 0;
}
int app_main(void)
{
    char line[128],cwd[256];struct app_args args;uint64_t last_status=0;
    uos_puts("OurOS EL0 shell. Type help; exit returns to monitor.\n");
    if (uos_argc()>1) {
        char option[16];if (uos_argc()!=2 || uos_arg(1,option,sizeof(option))<0 || !same(option,"--noprofile")) {
            uos_puts("Usage: SHELL.ELF [--noprofile]\n");return 2;
        }
    }else {
        struct uos_entry entry;if (!uos_stat("/data/session.env",&entry)) {
            if (uos_profile(UOS_PROFILE_LOAD,"/data/session.env")) { uos_puts("shell: profile refused; environment retained\n"); }
            else { uos_puts("shell: profile loaded /data/session.env\n"); }
        }
    }
    for (;;) {
        if (uos_getcwd(cwd,sizeof(cwd))<0) { return 1; }
        uos_puts("user:");uos_puts(cwd);uos_puts("$ ");
        int64_t n=uos_readline(line,sizeof(line));if (n==UOS_EOF) { return 0; }
        if (n<0) { last_status=2;uos_puts("shell: input rejected; command not executed\n");if (n==UOS_ERROR) { continue; }return 1; }
        if (!n) { continue; }
        if (!app_args_expand(line,expand_value,&last_status,&args)) { last_status=2;uos_puts("shell: invalid quoting, expansion or argument bounds\n");continue; }
        const char *cmd=args.bytes,*p=args.count>1 ? args.bytes+args.offset[1] : ".";int result=0;
        if (!same(cmd,"status")) { last_status=0; }
        if (same(cmd,"exit") && args.count==1) { return 0; }
        if (same(cmd,"help") && args.count==1) {
            uos_puts("help pwd cd ls stat cat echo write mkdir rm rmdir cp mv replace sha256 sync status path exit\nrun|exec PROGRAM [args]\nrunio IN|- OUT|- PROGRAM [args]\npipe PROGRAM [args] -- PROGRAM [args]\npipeio IN|- OUT|- PROGRAM [args] -- PROGRAM [args]\nenv | getenv NAME | setenv NAME VALUE | unsetenv NAME\nprofile save|load [path]\n$NAME ${NAME} $?; single quotes keep literals\n");
        } else if (same(cmd,"status") && args.count==1) { number(last_status);uos_puts("\n"); }
        else if ((same(cmd,"path") && args.count==1) || (same(cmd,"getenv") && args.count==2) || (same(cmd,"env") && args.count==1)) {
            result=environment(same(cmd,"env") ? 0 : same(cmd,"path") ? "PATH" : p);last_status=result ? 1 : 0;
        }
        else if ((same(cmd,"setenv") && args.count==3) || (same(cmd,"unsetenv") && args.count==2)) {
            int64_t r=same(cmd,"setenv") ? uos_setenv(p,args.bytes+args.offset[2]) : uos_unsetenv(p);
            result=r!=0;last_status=result ? 1 : 0;if (r==UOS_ENOSPC) { uos_puts("shell: environment is full (8 variables)\n"); }
        }
        else if (same(cmd,"profile") && (args.count==2 || args.count==3) && (same(p,"save") || same(p,"load"))) {
            const char *path=args.count==3 ? args.bytes+args.offset[2] : "/data/session.env";bool save=same(p,"save");
            int64_t r=uos_profile(save ? UOS_PROFILE_SAVE : UOS_PROFILE_LOAD,path);last_status=r ? 1 : 0;
            if (r) { uos_puts("shell: profile refused; environment retained\n"); }
            else { uos_puts(save ? "Profile saved: " : "Profile loaded: ");uos_puts(path);uos_puts("\n"); }
        }
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
                if (separators!=1 || split==first || split+1==args.count) { last_status=2;uos_puts("shell: use pipe <ELF> [args] -- <ELF> [args]\n");continue; }
            }
            words(&child,&args,first,split);int64_t code;unsigned seconds=first==3 ? 30 : 10;
            if (program(&child)) { last_status=127;uos_puts("shell: program not found\n");continue; }
            if (piping) {
                words(&right,&args,split+1,args.count);
                if (program(&right)) { last_status=127;uos_puts("shell: program not found\n");continue; }
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
            if (r==UOS_ENOENT) { last_status=1;uos_puts("shell: file not found: ");uos_puts(p);uos_puts("\n");continue; }
            if (!result) { for (unsigned i=0;i<32;++i) { hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15]; }hex[64]=0;uos_puts(hex);uos_puts("  ");uos_puts(p);uos_puts("\n"); }
        }
        else if (same(cmd,"pwd") && args.count==1) { uos_puts(cwd);uos_puts("\n"); }
        else if (same(cmd,"cd") && args.count<=2) {
            char home[APP_ENV_VALUE_BYTES];if (args.count==1) {
                if (uos_getenv("HOME",home,sizeof(home))<0) { result=1; }else { result=uos_chdir(home)!=0; }
            }else { result=uos_chdir(p)!=0; }
        }
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
        if (result) { last_status=1;uos_puts("shell: operation failed\n"); }
    }
}
