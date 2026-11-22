/* Original serial EL0 shell. All filesystem/terminal work crosses SVC;
 * builtins execute in this process, with no kernel command interpreter calls. */
#include "user.h"
#include "app_args.h"
/* Keep repeated console setup out of the command dispatcher. */
static __attribute__((noinline,noclone)) void print(const char *text) { (void)uos_puts(text); }
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool monitor_network_command(const char *line)
{
    static const char *const commands[]={"netup","netping","netrenew","netinfo","netdown","networktest"};
    while (*line==' ' || *line=='\t') { ++line; }
    for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
        const char *name=commands[i],*word=line;
        while (*name && *name==*word) { ++name;++word; }
        if (!*name && (!*word || *word==' ' || *word=='\t')) { return true; }
    }
    return false;
}
static void monitor_network_hint(void)
{
    print("Type exit, then run this command at the > monitor prompt. For the EL0 test use networktest el0 (lowercase L, digit zero).\n");
}
enum shell_command { CMD_OTHER=0,CMD_JOBS,CMD_STOP,CMD_STATUS,CMD_EXIT,CMD_HELP,CMD_FG,CMD_PATH,CMD_GETENV,CMD_ENV,CMD_SETENV,CMD_UNSETENV,CMD_PROFILE,CMD_EXEC,CMD_RUN,CMD_RUNIO,CMD_PIPE,CMD_PIPEIO,CMD_BG,CMD_BGIO,CMD_CP,CMD_MV,CMD_REPLACE,CMD_STAT,CMD_SYNC,CMD_SHA256,CMD_PWD,CMD_CD,CMD_LS,CMD_CAT,CMD_ECHO,CMD_WRITE,CMD_MKDIR,CMD_RM,CMD_RMDIR,CMD_SOURCE };
static unsigned command_kind(const char *cmd)
{
    const char *name="jobs\0""stop\0""status\0""exit\0""help\0""fg\0""path\0""getenv\0""env\0""setenv\0""unsetenv\0""profile\0""exec\0""run\0""runio\0""pipe\0""pipeio\0""bg\0""bgio\0""cp\0""mv\0""replace\0""stat\0""sync\0""sha256\0""pwd\0""cd\0""ls\0""cat\0""echo\0""write\0""mkdir\0""rm\0""rmdir\0""source\0";unsigned kind=1;
    while (*name) { if (same(cmd,name)) { return kind; }while (*name++) { }++kind; }return CMD_OTHER;
}
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
    if (code<0) { *last_status=127;print("shell: launch/wait failed; check ELF/input/output paths\n"); }
    else { *last_status=(uint64_t)code;print(piping ? "pipeline exit=" : "child exit=");number(*last_status);print("\n"); }
}
static unsigned timeout(const char *s)
{
    unsigned n=0;for (unsigned i=0;s[i];++i) { if (s[i]<'0' || s[i]>'9' || n>30) { return 0; }n=n*10+(unsigned)(s[i]-'0'); }
    return n<=30 ? n : 0;
}
enum { JOB_LIMIT=2 };
static uint64_t job_pid(const char *text)
{
    uint64_t n=0;
    for (;*text;++text) {
        unsigned digit=(unsigned)(*text-'0');
        if (digit>9 || n>(UINT64_MAX-digit)/10) { return 0; }
        n=n*10+digit;
    }
    return n;
}
static void job_command(unsigned kind,uint64_t jobs[JOB_LIMIT],const char *selector,uint64_t *status)
{
    if (!jobs[0] && !jobs[1]) { print("No background job.\n");*status=(kind==CMD_JOBS) ? 0 : 1;return; }
    if (kind==CMD_JOBS) {
        for (unsigned i=0;i<JOB_LIMIT;++i) {
            if (!jobs[i]) { continue; }
            struct uos_child_info info;if (uos_child_poll(jobs[i],&info)) { *status=1;print("shell: job error\n");return; }
            print("job pid=");number(jobs[i]);print(info.finished ? " done code=" : " pending");
            if (info.finished) { number((unsigned)info.exit_code); }print("\n");
        }
        return;
    }
    if (!selector && jobs[0] && jobs[1]) { *status=2;print("shell: specify a job PID\n");return; }
    uint64_t id=selector ? job_pid(selector) : jobs[0] ? jobs[0] : jobs[1];
    if (!id || (id!=jobs[0] && id!=jobs[1])) { *status=1;print("shell: unknown job PID\n");return; }
    uint64_t *pid=&jobs[id==jobs[0] ? 0 : 1];
    if (kind==CMD_STOP && uos_child_cancel(*pid)) {
        *status=1;print("shell: job error\n");return;
    }
    int64_t result=uos_wait(*pid);if (result>=0) { *pid=0; }job_result(result,0,status);
}
static int list(const char *path)
{
    struct uos_entry e;if (uos_stat(path,&e)) { return 1; }
    if (e.type==UOS_FILE) { number(e.bytes);print(" ");print(path);print("\n");return 0; }
    for (unsigned i=0;i<1024;++i) {
        int64_t r=uos_readdir(path,i,&e);if (r<0) { return 1; }if (!r) { return 0; }
        print(e.type==UOS_DIRECTORY ? "d " : "f ");if (e.type==UOS_FILE) { number(e.bytes);print(" "); }
        print(e.name);print(e.type==UOS_DIRECTORY ? "/\n" : "\n");
    }return 1;
}
static int cat(const char *path)
{
    char data[512];int64_t h=uos_open(path,UOS_READ);if (h<0) { return 1; }int result=0;
    for (;;) {
        int64_t n=uos_read((uint64_t)h,data,sizeof(data));if (n<0) { result=1;break; }if (!n) { break; }
        if (uos_write(data,(size_t)n)!=n) { result=1;break; }
    }
    if (uos_close((uint64_t)h)) { result=1; }print("\n");return result;
}
static int environment(const char *key)
{
    if (key) {
        char value[APP_ENV_VALUE_BYTES];if (uos_getenv(key,value,sizeof(value))<0) { return 1; }print(value);print("\n");return 0;
    }
    struct app_env_entry entry;
    for (unsigned i=0;i<APP_ENV_LIMIT;++i) {
        int64_t r=uos_env_entry(i,&entry);if (r<0) { return 1; }if (!r) { break; }
        print(entry.name);print("=");print(entry.value);print("\n");
    }return 0;
}
enum { SHELL_LINE_BYTES=128, COMMAND_LIMIT=8, SCRIPT_BYTES=4096, SCRIPT_LINES=32 };
enum list_operator { LIST_ALWAYS,LIST_AND,LIST_OR };
struct command_list {
    unsigned count;
    struct { char *text;enum list_operator op; } entries[COMMAND_LIMIT];
};
struct shell_state { uint64_t status,jobs[JOB_LIMIT];bool exiting,sourcing; };

/* Split before expanding so a variable's value cannot introduce operators.
 * Match the argument parser's quote and backslash handling. */
static bool split_commands(char *line,struct command_list *list)
{
    list->count=0;
    while (*line==' ' || *line=='\t') { ++line; }
    if (!*line || *line=='#') { return true; }
    char quote=0,*start=line;
    bool word=false;
    enum list_operator op=LIST_ALWAYS;
    for (char *p=line;;++p) {
        char c=*p;
        if (c=='\\') {
            if (!p[1]) { return false; }
            word=true;++p;continue;
        }
        if (c=='\'' || c=='"') {
            if (!quote || quote==c) { quote=quote ? 0 : c; }
            word=true;continue;
        }
        bool pair=!quote && (c=='&' || c=='|') && p[1]==c;
        if (!c || (!quote && c==';') || pair) {
            if (quote || !word || list->count==COMMAND_LIMIT) { return false; }
            list->entries[list->count].text=start;list->entries[list->count++].op=op;
            if (!c) { return true; }
            *p=0;
            op=pair ? (c=='&' ? LIST_AND : LIST_OR) : LIST_ALWAYS;
            if (pair) { ++p; }
            start=p+1;word=false;
        } else if (c!=' ' && c!='\t') { word=true; }
    }
}
static bool execute_line(struct shell_state *state,char *line);
static void source_file(struct shell_state *state,const char *path)
{
    char text[SCRIPT_BYTES+1];size_t bytes=0;
    int64_t handle=uos_open(path,UOS_READ),got=0;
    if (handle<0) { state->status=1;print("shell: cannot open script\n");return; }
    while (bytes<sizeof(text)) {
        size_t left=sizeof(text)-bytes;
        got=uos_read((uint64_t)handle,text+bytes,left<512 ? left : 512);
        if (got<=0) { break; }
        bytes+=(size_t)got;
    }
    if (uos_close((uint64_t)handle) || got<0) { state->status=1;print("shell: cannot read script\n");return; }
    char *lines[SCRIPT_LINES];unsigned count=0;
    if (bytes>SCRIPT_BYTES) { goto invalid; }
    for (size_t start=0;start<bytes;) {
        size_t end=start;
        while (end<bytes && text[end]!='\n') { ++end; }
        size_t last=end;
        if (end<bytes && last>start && text[last-1]=='\r') { --last; }
        if (last-start>=SHELL_LINE_BYTES || count==SCRIPT_LINES) { goto invalid; }
        for (size_t i=start;i<last;++i) {
            unsigned char c=(unsigned char)text[i];
            if (c!='\t' && (c<32 || c>126)) { goto invalid; }
        }
        text[last]=0;lines[count++]=text+start;start=end+1;
    }
    state->sourcing=true;
    for (unsigned i=0;i<count && !state->exiting;++i) { if (!execute_line(state,lines[i])) { break; } }
    state->sourcing=false;
    return;
invalid:
    state->status=2;print("shell: invalid script text or size limits\n");
}
static bool execute_line(struct shell_state *state,char *line)
{
    struct command_list commands;
    if (!split_commands(line,&commands)) {
        state->status=2;
        if (monitor_network_command(line)) { monitor_network_hint(); }
        else { print("shell: invalid quoting, expansion or argument bounds (command list)\n"); }
        return false;
    }
    for (unsigned step=0;step<commands.count && !state->exiting;++step) {
        enum list_operator op=commands.entries[step].op;
        if ((op==LIST_AND && state->status) || (op==LIST_OR && !state->status)) { continue; }
        struct app_args args;
        if (!app_args_expand(commands.entries[step].text,expand_value,&state->status,&args)) {
            state->status=2;print("shell: invalid quoting, expansion or argument bounds\n");return false;
        }
        const char *p=args.count>1 ? args.bytes+args.offset[1] : ".";unsigned kind=command_kind(args.bytes);int result=0;
        if (!(kind==CMD_STATUS)) { state->status=0; }
        if (kind==CMD_EXIT && args.count==1) { state->exiting=true;return true; }
        if ((kind==CMD_HELP) && args.count==1) {
            print("help pwd cd ls stat cat echo write mkdir rm rmdir cp mv replace sha256 sync status path exit\nrun|exec PROGRAM [args]\nrunio IN|- OUT|- PROGRAM [args]\npipe PROGRAM [args] -- PROGRAM [args]\npipeio IN|- OUT|- PROGRAM [args] -- PROGRAM [args]\n");
            print("bg SECONDS PROGRAM [args]\nbgio SECONDS IN|- OUT|- PROGRAM [args]\njobs | fg [PID] | stop [PID] (two jobs, 1..30 seconds)\nenv | getenv NAME | setenv NAME VALUE | unsetenv NAME\nprofile save|load [path]\nsource PATH | lists with ; && ||\n$NAME ${NAME} $?; single quotes keep literals\n");
            print("Use exit for the kernel monitor (Wi-Fi commands and networktest).\n");
        } else if ((kind==CMD_STATUS) && args.count==1) { number(state->status);print("\n"); }
        else if (kind==CMD_SOURCE && args.count==2) {
            if (state->sourcing) { state->status=2;print("shell: nested source refused\n"); }
            else { source_file(state,p); }
        }
        else if ((kind==CMD_JOBS && args.count==1) || ((kind==CMD_FG || kind==CMD_STOP) && args.count<=2)) {
            job_command(kind,state->jobs,args.count==2 ? p : 0,&state->status);
        }
        else if (((kind==CMD_PATH) && args.count==1) || ((kind==CMD_GETENV) && args.count==2) || ((kind==CMD_ENV) && args.count==1)) {
            result=environment((kind==CMD_ENV) ? 0 : (kind==CMD_PATH) ? "PATH" : p);state->status=result ? 1 : 0;
        }
        else if (((kind==CMD_SETENV) && args.count==3) || ((kind==CMD_UNSETENV) && args.count==2)) {
            int64_t r=(kind==CMD_SETENV) ? uos_setenv(p,args.bytes+args.offset[2]) : uos_unsetenv(p);
            result=r!=0;state->status=result ? 1 : 0;if (r==UOS_ENOSPC) { print("shell: environment is full (8 variables)\n"); }
        }
        else if ((kind==CMD_PROFILE) && (args.count==2 || args.count==3) && (same(p,"save") || same(p,"load"))) {
            const char *path=args.count==3 ? args.bytes+args.offset[2] : "/data/session.env";bool save=same(p,"save");
            int64_t r=uos_profile(save ? UOS_PROFILE_SAVE : UOS_PROFILE_LOAD,path);state->status=r ? 1 : 0;
            if (r) { print("shell: profile refused; environment retained\n"); }
            else { print(save ? "Profile saved: " : "Profile loaded: ");print(path);print("\n"); }
        }
        else if ((kind==CMD_EXEC) && args.count>=2) {
            if (state->jobs[0] || state->jobs[1]) { state->status=1;print("shell: use fg or stop first\n");continue; }
            struct app_args next;words(&next,&args,1,args.count);
            int64_t r=program(&next);if (!r) { r=uos_exec(&next); }
            state->status=127;print(r==UOS_ENOENT ? "shell: program not found\n" : "shell: exec failed; current shell retained\n");
        }
        else if (((kind==CMD_RUN) && args.count>=2) || ((kind==CMD_RUNIO) && args.count>=4) ||
                 ((kind==CMD_PIPE) && args.count>=4) || ((kind==CMD_PIPEIO) && args.count>=6) ||
                 ((kind==CMD_BG) && args.count>=3) || ((kind==CMD_BGIO) && args.count>=5)) {
            struct app_args child,right;struct uos_stdio io={0};
            bool piping=(kind==CMD_PIPE) || (kind==CMD_PIPEIO),background=(kind==CMD_BG) || (kind==CMD_BGIO);
            if ((piping && (state->jobs[0] || state->jobs[1])) || (state->jobs[0] && state->jobs[1])) {
                state->status=1;print("shell: use fg or stop first\n");continue;
            }
            bool redirected=(kind==CMD_RUNIO) || (kind==CMD_PIPEIO) || (kind==CMD_BGIO);
            unsigned first=(redirected ? 3u : 1u)+(background ? 1u : 0u),seconds=redirected ? 30 : 10;
            if (background && !(seconds=timeout(p))) { state->status=2;print("shell: timeout must be 1..30s\n");continue; }
            if (redirected) {
                for (unsigned i=0;i<2;++i) {
                    const char *path=args.bytes+args.offset[i+1+(background ? 1u : 0u)];char *target=i ? io.output : io.input;
                    if (!same(path,"-")) { do { *target++=*path; }while (*path++); }
                }
            }
            unsigned split=args.count;
            if (piping) {
                unsigned separators=0;
                for (unsigned i=first;i<args.count;++i) { if (same(args.bytes+args.offset[i],"--")) { split=i;++separators; } }
                if (separators!=1 || split==first || split+1==args.count) { state->status=2;print("shell: use pipe <ELF> [args] -- <ELF> [args]\n");continue; }
            }
            words(&child,&args,first,split);int64_t code;
            if (program(&child)) { state->status=127;print("shell: program not found\n");continue; }
            if (piping) {
                words(&right,&args,split+1,args.count);
                if (program(&right)) { state->status=127;print("shell: program not found\n");continue; }
                code=uos_pipeline(&child,&right,seconds,redirected ? &io : 0);
            }
            else {
                int64_t pid=redirected ? uos_spawn_io(&child,seconds,&io) : uos_spawn(&child,seconds);
                if (background && pid>=0) { state->jobs[state->jobs[0]!=0]=(uint64_t)pid;print("job pid=");number((uint64_t)pid);print("\n");continue; }
                code=pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid);
            }
            job_result(code,piping,&state->status);
        }
        else if ((kind==CMD_CP) && args.count==3) { result=uos_copy(p,args.bytes+args.offset[2])<0; }
        else if (((kind==CMD_MV) || (kind==CMD_REPLACE)) && args.count==3) {
            int64_t r=(kind==CMD_REPLACE) ? uos_publish(p,args.bytes+args.offset[2]) : uos_rename(p,args.bytes+args.offset[2]);state->status=r ? 1 : 0;
            if (r) { print(r==UOS_EEXIST ? "shell: target exists; mv does not overwrite\n" :
                r==UOS_EBUSY ? "shell: file is open; close handles first\n" :
                r==UOS_EXDEV ? "shell: operation requires the same filesystem\n" :
                r==UOS_EROFS ? "shell: read-only filesystem\n" :
                r==UOS_EISDIR ? "shell: operation accepts regular files only\n" :
                r==UOS_ENOENT ? "shell: source or parent not found\n" : "shell: rename failed\n"); }
        }
        else if ((kind==CMD_STAT) && args.count==2) {
            struct uos_entry e;result=uos_stat(p,&e)!=0;
            if (!result) { print(e.type==UOS_FILE ? "type=file bytes=" : "type=directory bytes=");number(e.bytes);print(" path=");print(p);print("\n"); }
        }
        else if ((kind==CMD_SYNC) && args.count==1) { result=uos_sync()!=0; }
        else if ((kind==CMD_SHA256) && args.count==2) {
            unsigned char digest[32];char hex[65];const char *digits="0123456789abcdef";
            int64_t r=uos_filehash(p,digest);result=r!=0;
            if (r==UOS_ENOENT) { state->status=1;print("shell: file not found: ");print(p);print("\n");continue; }
            if (!result) { for (unsigned i=0;i<32;++i) { hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15]; }hex[64]=0;print(hex);print("  ");print(p);print("\n"); }
        }
        else if (kind==CMD_PWD && args.count==1) { char cwd[256];result=uos_getcwd(cwd,sizeof(cwd))<0;if (!result) { print(cwd);print("\n"); } }
        else if ((kind==CMD_CD) && args.count<=2) {
            char home[APP_ENV_VALUE_BYTES];if (args.count==1) {
                if (uos_getenv("HOME",home,sizeof(home))<0) { result=1; }else { result=uos_chdir(home)!=0; }
            }else { result=uos_chdir(p)!=0; }
        }
        else if ((kind==CMD_LS) && args.count<=2) { result=list(p); }
        else if ((kind==CMD_CAT) && args.count==2) { result=cat(p); }
        else if ((kind==CMD_ECHO)) {
            for (unsigned i=1;i<args.count;++i) { if (i>1) { print(" "); }print(args.bytes+args.offset[i]); }print("\n");
        } else if ((kind==CMD_WRITE) && args.count==3) {
            const char *data=args.bytes+args.offset[2];size_t bytes=uos_length(data);result=uos_replace(p,data,bytes)!=(int64_t)bytes;
        } else if ((kind==CMD_MKDIR) && args.count==2) { result=uos_mkdir(p)!=0; }
        else if (((kind==CMD_RM) || (kind==CMD_RMDIR)) && args.count==2) { result=uos_remove(p,(kind==CMD_RMDIR))!=0; }
        else if (monitor_network_command(args.bytes)) { state->status=2;monitor_network_hint(); }
        else {
            if (state->jobs[0] && state->jobs[1]) { state->status=1;print("shell: use fg or stop first\n");continue; }
            if (program(&args)) { state->status=127;print("shell: unknown command or arguments; type help\n");continue; }
            int64_t pid=uos_spawn(&args,10);job_result(pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid),0,&state->status);
        }
        if (result) { state->status=1;print("shell: operation failed\n"); }
    }
    return true;
}
int app_main(void)
{
    char line[SHELL_LINE_BYTES],cwd[256];struct shell_state state={0};
    print("OurOS EL0 shell. Type help; exit returns to monitor.\n");
    if (uos_argc()>1) {
        char option[16];if (uos_argc()!=2 || uos_arg(1,option,sizeof(option))<0 || !same(option,"--noprofile")) {
            print("Usage: SHELL.ELF [--noprofile]\n");return 2;
        }
    }else {
        struct uos_entry entry;if (!uos_stat("/data/session.env",&entry)) {
            if (uos_profile(UOS_PROFILE_LOAD,"/data/session.env")) { print("shell: profile refused; environment retained\n"); }
            else { print("shell: profile loaded /data/session.env\n"); }
        }
    }
    while (!state.exiting) {
        if (uos_getcwd(cwd,sizeof(cwd))<0) { return 1; }
        print("user:");print(cwd);print("$ ");
        int64_t n=uos_readline(line,sizeof(line));if (n==UOS_EOF) { break; }
        if (n<0) { state.status=2;print("shell: input rejected; command not executed\n");if (n==UOS_ERROR) { continue; }return 1; }
        if (n) { (void)execute_line(&state,line); }
    }
    return 0;
}
