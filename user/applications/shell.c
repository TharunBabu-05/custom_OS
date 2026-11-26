/* Original serial EL0 shell. All filesystem/terminal work crosses SVC;
 * builtins execute in this process, with no kernel command interpreter calls. */
#include "user.h"
#include "app_args.h"
/* Keep repeated console setup out of the command dispatcher. */
static __attribute__((noinline,noclone)) void print(const char *text) { (void)uos_puts(text); }
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool monitor_command(const char *line,const char *names)
{
    while (*line==' ' || *line=='\t') { ++line; }
    while (*names) {
        const char *name=names,*word=line;
        while (*name && *name==*word) { ++name;++word; }
        if (!*name && (!*word || *word==' ' || *word=='\t')) { return true; }
        while (*name++) { }
        names=name;
    }
    return false;
}
static bool monitor_hint(const char *line)
{
    bool network=monitor_command(line,"netup\0netping\0netrenew\0netinfo\0netdown\0networktest\0");
    if (!network && !monitor_command(line,"smpstart\0smpinfo\0smptest\0smpfault\0smpstop\0smppark\0"
        "smpsubmit\0smpwait\0smpcancel\0smpirq\0smpipi\0smptlb\0smpqueue\0smplock\0"
        "smpmem\0smpmeminfo\0cpustate\0meminfo\0fileinfo\0storagetest\0datacheck\0")) {
        return false;
    }
    print("Type exit, then run this command at the > monitor prompt.");
    print(network ? " For the EL0 test use networktest el0 (lowercase L, digit zero).\n" : "\n");
    return true;
}
enum shell_command { CMD_OTHER=0,CMD_JOBS,CMD_STOP,CMD_STATUS,CMD_EXIT,CMD_HELP,CMD_FG,CMD_PATH,CMD_GETENV,CMD_ENV,CMD_SETENV,CMD_UNSETENV,CMD_PROFILE,CMD_EXEC,CMD_RUN,CMD_RUNIO,CMD_PIPE,CMD_PIPEIO,CMD_BG,CMD_BGIO,CMD_CP,CMD_MV,CMD_REPLACE,CMD_STAT,CMD_SYNC,CMD_SHA256,CMD_PWD,CMD_CD,CMD_LS,CMD_CAT,CMD_ECHO,CMD_WRITE,CMD_MKDIR,CMD_RM,CMD_RMDIR,CMD_SOURCE,CMD_HISTORY,CMD_SCHED,CMD_WAITALL,CMD_STOPALL,CMD_VMSTAT,CMD_POLICY,CMD_RUNLIM };
static unsigned command_kind(const char *cmd)
{
    const char *name="jobs\0""stop\0""status\0""exit\0""help\0""fg\0""path\0""getenv\0""env\0""setenv\0""unsetenv\0""profile\0""exec\0""run\0""runio\0""pipe\0""pipeio\0""bg\0""bgio\0""cp\0""mv\0""replace\0""stat\0""sync\0""sha256\0""pwd\0""cd\0""ls\0""cat\0""echo\0""write\0""mkdir\0""rm\0""rmdir\0""source\0""history\0""sched\0""waitall\0""stopall\0""vmstat\0""policy\0""runlim\0";unsigned kind=1;
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
enum { JOB_LIMIT=UOS_JOB_MAX };
static unsigned job_count(const uint64_t jobs[JOB_LIMIT])
{
    unsigned count=0;
    for (unsigned i=0;i<JOB_LIMIT;++i) { count+=jobs[i]!=0; }
    return count;
}
static unsigned job_slot(const uint64_t jobs[JOB_LIMIT],uint64_t pid)
{
    unsigned slot=0;
    while (slot<JOB_LIMIT && jobs[slot]!=pid) { ++slot; }
    return slot;
}
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
static __attribute__((noinline)) unsigned vmstat_command(const struct app_args *args)
{
    uint64_t pid=0;
    if (args->count>2 || (args->count==2 && !(pid=job_pid(args->bytes+args->offset[1])))) {
        print("Usage: vmstat [PID]\n");return 2;
    }
    struct uos_vm_info info;
    if (uos_vm_info(pid,&info) || info.count>UOS_VM_MAX_REGIONS) { print("vmstat: process unavailable\n");return 1; }
    print("vm pid=");number(info.pid);print(" pages=");number(info.pages);print("/");number(info.page_limit);
    print(" regions=");number(info.count);print("\n");
    for (unsigned i=0;i<info.count;++i) {
        const struct uos_vm_entry *e=&info.entries[i];
        print(e->flags&UOS_VM_FILE_BACKED ? "file" : "anonymous");
        print(e->flags&UOS_VM_PEER ? " peer" : e->flags&UOS_VM_SHARED ? " shared" : " private");
        print(" bytes=");number(e->bytes);print(" pages=");number(e->pages);
        print(e->rights&UOS_WRITE ? " rights=rw" : " rights=r");
        print(e->ceiling&UOS_WRITE ? " ceiling=rw\n" : " ceiling=r\n");
    }
    return 0;
}
static unsigned policy_command(const struct app_args *args)
{
    uint64_t pid=0;
    if (args->count>2 || (args->count==2 && !(pid=job_pid(args->bytes+args->offset[1])))) {
        print("Usage: policy [PID]\n");return 2;
    }
    int64_t mask=uos_policy_get(pid);
    if (mask<0) { print("policy: process unavailable\n");return 1; }
    print("policy mask=");number((uint64_t)mask);print("\n");return 0;
}
static __attribute__((noinline)) void run_limited(const struct app_args *args,uint64_t *status)
{
    unsigned mask;
    if (args->count<3 || !app_args_uint(args->bytes+args->offset[1],UOS_POLICY_ALL,&mask)) {
        *status=2;print("Usage: runlim MASK PROGRAM [args]\n");return;
    }
    struct app_args command;
    words(&command,args,2,args->count);
    if (program(&command)) { *status=127;print("shell: program not found\n");return; }
    int64_t pid=uos_spawn_limited(&command,30,0,0,mask);
    if (pid==UOS_EACCES) { *status=1;print("runlim: launch refused by policy\n");return; }
    job_result(pid<0 ? pid : uos_wait((uint64_t)pid),0,status);
}
static unsigned scheduling_command(const struct app_args *args)
{
    uint64_t pid=0;
    if (args->count>3 || (args->count>1 && !(pid=job_pid(args->bytes+args->offset[1])))) { goto usage; }
    if (args->count==3) {
        const char *priority=args->bytes+args->offset[2];
        unsigned quantum=same(priority,"low") ? UOS_SCHED_LOW : same(priority,"normal") ? UOS_SCHED_NORMAL :
                         same(priority,"high") ? UOS_SCHED_HIGH : 0;
        if (!quantum) { goto usage; }
        if (uos_sched_set(pid,quantum)) { print("sched: priority change refused\n");return 1; }
    }
    struct uos_sched_info info;
    if (uos_sched_get(pid,&info)) { print("sched: process unavailable\n");return 1; }
    const char *level=info.quantum_ticks==UOS_SCHED_LOW ? "low" : info.quantum_ticks==UOS_SCHED_HIGH ? "high" : "normal";
    print("pid=");number(info.pid);print(" priority=");print(level);
    print(" quantum_ticks=");number(info.quantum_ticks);
    print(" user_ticks=");number(info.user_ticks);print(" ready_ticks=");number(info.ready_ticks);print("\n");
    return 0;
usage:
    print("Usage: sched [PID [low|normal|high]]\n");
    return 2;
}
static int64_t collect_job(uint64_t *pid)
{
    int64_t result=uos_wait(*pid);
    if (result>=0) { *pid=0; }
    return result;
}
static unsigned collect_jobs(uint64_t jobs[JOB_LIMIT],bool cancel)
{
    bool failed=false;
    if (cancel) {
        for (unsigned i=0;i<JOB_LIMIT;++i) {
            if (jobs[i] && uos_child_cancel(jobs[i])) { failed=true; }
        }
    }
    unsigned status=0;
    for (unsigned i=0;i<JOB_LIMIT;++i) {
        uint64_t pid=jobs[i];if (!pid) { continue; }
        int64_t result=collect_job(&jobs[i]);
        if (result<0) { failed=true;continue; }
        print("job pid=");number(pid);print(" exit=");number((uint64_t)result);print("\n");
        if (!cancel && !status) { status=(unsigned)result; }
    }
    if (failed) { print("shell: job collection failed\n");return 1; }
    return status;
}
static void job_command(unsigned kind,uint64_t jobs[JOB_LIMIT],const char *selector,uint64_t *status)
{
    unsigned count=job_count(jobs);
    if (!count) { print("No background job.\n");*status=(kind==CMD_FG || kind==CMD_STOP);return; }
    if (kind==CMD_WAITALL || kind==CMD_STOPALL) { *status=collect_jobs(jobs,kind==CMD_STOPALL);return; }
    if (kind==CMD_JOBS) {
        for (unsigned i=0;i<JOB_LIMIT;++i) {
            if (!jobs[i]) { continue; }
            struct uos_child_info info;if (uos_child_poll(jobs[i],&info)) { *status=1;print("shell: job error\n");return; }
            print("job pid=");number(jobs[i]);print(info.finished ? " done code=" : " pending");
            if (info.finished) { number((unsigned)info.exit_code); }print("\n");
        }
        return;
    }
    if (!selector && count>1) { *status=2;print("shell: specify a job PID\n");return; }
    unsigned slot=0;
    if (selector) {
        uint64_t id=job_pid(selector);
        slot=id ? job_slot(jobs,id) : JOB_LIMIT;
    } else {
        while (!jobs[slot]) { ++slot; }
    }
    if (slot==JOB_LIMIT) { *status=1;print("shell: unknown job PID\n");return; }
    uint64_t *pid=&jobs[slot];
    if (kind==CMD_STOP && uos_child_cancel(*pid)) {
        *status=1;print("shell: job error\n");return;
    }
    job_result(collect_job(pid),0,status);
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
enum { SHELL_LINE_BYTES=128, COMMAND_LIMIT=8, SCRIPT_BYTES=4096, SCRIPT_LINES=32, HISTORY_LIMIT=16 };
_Static_assert(HISTORY_LIMIT*SHELL_LINE_BYTES<=4096,"history must fit in one private page");
enum list_operator { LIST_ALWAYS,LIST_AND,LIST_OR };
struct command_list {
    unsigned count;
    struct { char *text;enum list_operator op; } entries[COMMAND_LIMIT];
};
struct shell_history {
    uint64_t last;
    unsigned count;
    char (*lines)[SHELL_LINE_BYTES];
};
struct shell_state {
    uint64_t status,jobs[JOB_LIMIT];
    bool exiting,sourcing,history_used;
    struct shell_history history;
};

static void copy_line(char *out,const char *text)
{
    while ((*out++=*text++)) { }
}

static unsigned history_slot(uint64_t id)
{ return (unsigned)((id-1)%HISTORY_LIMIT); }

static void history_add(struct shell_history *history,const char *line)
{
    if (!history->lines || history->last==UINT64_MAX) { return; }
    copy_line(history->lines[history_slot(++history->last)],line);
    if (history->count<HISTORY_LIMIT) { ++history->count; }
}

static void history_command(struct shell_state *state,const struct app_args *args)
{
    struct shell_history *history=&state->history;
    state->history_used=true;
    if (!history->lines) { state->status=1;print("shell: history storage unavailable\n");return; }
    if (args->count==2 && same(args->bytes+args->offset[1],"-c")) {
        char *bytes=(char *)history->lines;
        for (size_t i=0;i<HISTORY_LIMIT*SHELL_LINE_BYTES;++i) { bytes[i]=0; }
        history->count=0;
        print("History cleared.\n");
    } else if (args->count!=1) {
        state->status=2;print("Usage: history [-c]\n");
    } else if (!history->count) {
        print("History is empty.\n");
    } else {
        uint64_t first=history->last-history->count+1;
        for (unsigned i=0;i<history->count;++i) {
            uint64_t id=first+i;
            number(id);print("  ");print(history->lines[history_slot(id)]);print("\n");
        }
    }
}

/* Recognize standalone !! or ! followed by a decimal ID before expansion. */
static bool history_recall(const struct shell_history *history,const char *text,const char **entry)
{
    if (*text!='!') { return false; }
    const char *p=text+1;
    uint64_t id=0;
    if (*p=='!') {
        id=history->last;++p;
    } else {
        if (*p<'0' || *p>'9') { return false; }
        bool overflow=false;
        while (*p>='0' && *p<='9') {
            unsigned digit=(unsigned)(*p++-'0');
            if (id>(UINT64_MAX-digit)/10) { overflow=true; }
            if (!overflow) { id=id*10+digit; }
        }
        if (overflow) { id=0; }
    }
    while (*p==' ' || *p=='\t') { ++p; }
    if (*p) { return false; }
    *entry=id && id<=history->last && history->last-id<history->count ?
        history->lines[history_slot(id)] : 0;
    return true;
}
enum stream_syntax { STREAM_PLAIN,STREAM_VALID,STREAM_INVALID };
struct stream_command {
    char stages[UOS_PIPE_STAGES][SHELL_LINE_BYTES];
    char input[SHELL_LINE_BYTES],output[SHELL_LINE_BYTES];
    unsigned count;
    bool append;
};

/* Operators are recognized only in the original text. Leave quote removal
 * and expansion to the same argument parser as ordinary commands. */
static const char *stream_word_end(const char *p)
{
    char quote=0;
    for (;*p;++p) {
        char c=*p;
        if (c=='\\' && p[1]) { ++p;continue; }
        if (c=='\'' || c=='"') {
            if (!quote || quote==c) { quote=quote ? 0 : c; }
        } else if (!quote && (c==' ' || c=='\t' || c=='|' || c=='<' || c=='>')) { break; }
    }
    return p;
}

static enum stream_syntax parse_stream(const char *p,struct stream_command *command)
{
    *command=(struct stream_command){.count=1};
    size_t used[UOS_PIPE_STAGES]={0};
    char *destination=0;
    bool operated=false;
    while (*p) {
        char c=*p;
        if (c==' ' || c=='\t') { ++p;continue; }
        unsigned stage=command->count-1;
        if (c=='|' || c=='<' || c=='>') {
            operated=true;
            if (destination) { return STREAM_INVALID; }
            if (c=='|') {
                if (command->count==UOS_PIPE_STAGES || !used[stage] || command->output[0]) { return STREAM_INVALID; }
                ++command->count;
            } else {
                destination=c=='<' ? command->input : command->output;
                if (*destination || (c=='<' && stage)) { return STREAM_INVALID; }
                if (c=='>' && p[1]=='>') { command->append=true;++p; }
            }
            ++p;
            continue;
        }
        const char *end=stream_word_end(p);
        size_t bytes=(size_t)(end-p);
        if (destination) {
            if (bytes>=SHELL_LINE_BYTES) { return STREAM_INVALID; }
            while (p<end) { *destination++=*p++; }
            *destination=0;
            destination=0;
        } else {
            char *text=command->stages[stage];
            size_t at=used[stage];
            if (at) { text[at++]=' '; }
            if (bytes>=SHELL_LINE_BYTES-at) { return STREAM_INVALID; }
            while (p<end) { text[at++]=*p++; }
            text[at]=0;
            used[stage]=at;
        }
    }
    if (!operated) { return STREAM_PLAIN; }
    return destination || !used[command->count-1] ? STREAM_INVALID : STREAM_VALID;
}

static bool stream_path(const char *text,char *path,size_t capacity,uint64_t *status)
{
    if (!*text) { return true; }
    struct app_args arg;
    if (!app_args_expand(text,expand_value,status,&arg) || arg.count!=1 || arg.used>capacity) { return false; }
    for (unsigned i=0;i<arg.used;++i) { path[i]=arg.bytes[i]; }
    return true;
}

static bool stream_program(const struct app_args *args)
{
    unsigned kind=command_kind(args->bytes);
    return kind==CMD_OTHER || kind==CMD_ECHO || kind==CMD_CAT || kind==CMD_LS || kind==CMD_PWD;
}

static void execute_stream(struct shell_state *state,const struct stream_command *command)
{
    struct app_args args[UOS_PIPE_STAGES];
    struct uos_stdio io={0};
    bool piping=command->count>1;
    unsigned jobs=job_count(state->jobs);
    if ((piping && jobs) || jobs==JOB_LIMIT) {
        state->status=1;print("shell: use fg or stop first\n");return;
    }
    if (!stream_path(command->input,io.input,sizeof(io.input),&state->status) ||
        !stream_path(command->output,io.output,sizeof(io.output),&state->status)) {
        state->status=2;print("shell: invalid arguments or redirection path\n");return;
    }
    for (unsigned i=0;i<command->count;++i) {
        if (!app_args_expand(command->stages[i],expand_value,&state->status,&args[i])) {
            state->status=2;print("shell: invalid arguments or redirection path\n");return;
        }
        if (!stream_program(&args[i])) {
            state->status=2;print("shell: use runnable programs with | < > >>\n");return;
        }
        if (program(&args[i])) { state->status=127;print("shell: program not found\n");return; }
    }
    int64_t code;
    if (piping) {
        code=uos_pipe_chain(args,command->count,30,&io,command->append ? UOS_PIPE_APPEND : 0);
    } else {
        int64_t pid=uos_spawn_ex(args,30,&io,command->append ? UOS_SPAWN_STDOUT_APPEND : 0);
        code=pid<0 ? pid : uos_wait((uint64_t)pid);
    }
    job_result(code,piping,&state->status);
}

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
static void source_file(struct shell_state *state,const char *path,bool stop_on_error)
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
    for (unsigned i=0;i<count && !state->exiting;++i) {
        bool valid=execute_line(state,lines[i]);
        if (stop_on_error && state->status) {
            print("source: stopped at line ");number(i+1);print(" status=");number(state->status);print("\n");
            break;
        }
        if (!valid) { break; }
    }
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
        if (!monitor_hint(line)) { print("shell: invalid quoting, expansion or argument bounds (command list)\n"); }
        return false;
    }
    struct stream_command stream;
    bool streams[COMMAND_LIMIT];
    for (unsigned i=0;i<commands.count;++i) {
        enum stream_syntax syntax=parse_stream(commands.entries[i].text,&stream);
        if (syntax==STREAM_INVALID) {
            state->status=2;print("shell: invalid pipe or redirection syntax\n");return false;
        }
        streams[i]=syntax==STREAM_VALID;
    }
    for (unsigned step=0;step<commands.count && !state->exiting;++step) {
        enum list_operator op=commands.entries[step].op;
        if ((op==LIST_AND && state->status) || (op==LIST_OR && !state->status)) { continue; }
        if (streams[step]) {
            (void)parse_stream(commands.entries[step].text,&stream);
            execute_stream(state,&stream);
            continue;
        }
        struct app_args args;
        if (!app_args_expand(commands.entries[step].text,expand_value,&state->status,&args)) {
            state->status=2;print("shell: invalid quoting, expansion or argument bounds\n");return false;
        }
        const char *p=args.count>1 ? args.bytes+args.offset[1] : ".";unsigned kind=command_kind(args.bytes);int result=0;
        if (!(kind==CMD_STATUS)) { state->status=0; }
        if (kind==CMD_EXIT && args.count==1) { state->exiting=true;return true; }
        if ((kind==CMD_HELP) && args.count==1) {
            print("help pwd cd ls stat cat echo write mkdir rm rmdir cp mv replace sha256 sync status path exit\nrun|exec PROGRAM [args]\nrunio IN|- OUT|- PROGRAM [args]\npipe PROGRAM [args] -- PROGRAM [args]\npipeio IN|- OUT|- PROGRAM [args] -- PROGRAM [args]\n");
            print("bg SECONDS PROGRAM [args]\nbgio SECONDS IN|- OUT|- PROGRAM [args]\njobs | fg [PID] | stop [PID] (four jobs, 1..30 seconds)\nwaitall | stopall\nenv | getenv NAME | setenv NAME VALUE | unsetenv NAME\nprofile save|load [path]\nsource [-e] PATH | lists with ; && ||\nsha256 PATH [expected-hex]\n$NAME ${NAME} $?; single quotes keep literals\n");
            print("Use exit for the kernel monitor (Wi-Fi commands and networktest).\n");
            print("PROGRAM < IN > NEWOUT | PROGRAM >> OUT | up to seven pipeline stages\n");
            print("history [-c] | !! repeats last | !N repeats entry N\n");
            print("sched [PID [low|normal|high]] | vmstat [PID]\n");
            print("ports | ipcecho call NAME MESSAGE\n");
            print("policy [PID] | runlim MASK PROGRAM [args]\n");
        } else if ((kind==CMD_STATUS) && args.count==1) { number(state->status);print("\n"); }
        else if (kind==CMD_HISTORY) { history_command(state,&args); }
        else if (kind==CMD_SCHED) { state->status=scheduling_command(&args); }
        else if (kind==CMD_VMSTAT) { state->status=vmstat_command(&args); }
        else if (kind==CMD_POLICY) { state->status=policy_command(&args); }
        else if (kind==CMD_RUNLIM) { run_limited(&args,&state->status); }
        else if (kind==CMD_SOURCE) {
            bool stop_on_error=args.count==3 && same(p,"-e");
            if (state->sourcing) { state->status=2;print("shell: nested source refused\n"); }
            else if (args.count==2 || stop_on_error) {
                source_file(state,stop_on_error ? args.bytes+args.offset[2] : p,stop_on_error);
            } else { state->status=2;print("Usage: source [-e] PATH\n"); }
        }
        else if (((kind==CMD_JOBS || kind==CMD_WAITALL || kind==CMD_STOPALL) && args.count==1) ||
                 ((kind==CMD_FG || kind==CMD_STOP) && args.count<=2)) {
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
            if (job_count(state->jobs)) { state->status=1;print("shell: use fg or stop first\n");continue; }
            struct app_args next;words(&next,&args,1,args.count);
            int64_t r=program(&next);if (!r) { r=uos_exec(&next); }
            state->status=127;print(r==UOS_ENOENT ? "shell: program not found\n" : "shell: exec failed; current shell retained\n");
        }
        else if (((kind==CMD_RUN) && args.count>=2) || ((kind==CMD_RUNIO) && args.count>=4) ||
                 ((kind==CMD_PIPE) && args.count>=4) || ((kind==CMD_PIPEIO) && args.count>=6) ||
                 ((kind==CMD_BG) && args.count>=3) || ((kind==CMD_BGIO) && args.count>=5)) {
            struct app_args child,right;struct uos_stdio io={0};
            bool piping=(kind==CMD_PIPE) || (kind==CMD_PIPEIO),background=(kind==CMD_BG) || (kind==CMD_BGIO);
            unsigned slot=job_slot(state->jobs,0);
            if ((piping && job_count(state->jobs)) || slot==JOB_LIMIT) {
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
                int64_t pid=uos_spawn_ex(&child,seconds,redirected ? &io : 0,0);
                if (background && pid>=0) { state->jobs[slot]=(uint64_t)pid;print("job pid=");number((uint64_t)pid);print("\n");continue; }
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
        else if ((kind==CMD_SHA256) && (args.count==2 || args.count==3)) {
            unsigned char digest[32];char hex[65];const char *digits="0123456789abcdef";
            const char *expected=args.count==3 ? args.bytes+args.offset[2] : NULL;
            if (expected) {
                bool valid=uos_length(expected)==64;
                for (unsigned i=0;valid && i<64;++i) {
                    char c=expected[i];
                    valid=(c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');
                }
                if (!valid) { state->status=2;print("sha256: expected 64 hex digits\n");continue; }
            }
            int64_t r=uos_filehash(p,digest);result=r!=0;
            if (r==UOS_ENOENT) { state->status=1;print("shell: file not found: ");print(p);print("\n");continue; }
            if (!result) {
                for (unsigned i=0;i<32;++i) { hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15]; }
                hex[64]=0;print(hex);print("  ");print(p);print("\n");
                if (expected) {
                    for (unsigned i=0;i<64;++i) {
                        char c=expected[i];
                        if (c>='A' && c<='F') { c+='a'-'A'; }
                        if (c!=hex[i]) { state->status=1;print("sha256: mismatch\n");break; }
                    }
                }
            }
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
        else if (monitor_hint(args.bytes)) { state->status=2; }
        else {
            if (job_count(state->jobs)==JOB_LIMIT) { state->status=1;print("shell: use fg or stop first\n");continue; }
            if (program(&args)) { state->status=127;print("shell: unknown command or arguments; type help\n");continue; }
            int64_t pid=uos_spawn_ex(&args,10,0,0);job_result(pid<0 ? UOS_ERROR : uos_wait((uint64_t)pid),0,&state->status);
        }
        if (result) { state->status=1;print("shell: operation failed\n"); }
    }
    return true;
}

static void submit_line(struct shell_state *state,char *line)
{
    const char *record=line,*entry;
    while (*record==' ' || *record=='\t') { ++record; }
    if (!*record || *record=='#') { return; }
    char work[SHELL_LINE_BYTES];
    if (history_recall(&state->history,record,&entry)) {
        if (!entry) { state->status=1;print("shell: history entry unavailable\n");return; }
        /* Copy the entry before running it because replay can clear history. */
        copy_line(line,entry);
        record=line;
        print("Replay: ");print(line);print("\n");
    }
    copy_line(work,record);
    state->history_used=false;
    if (execute_line(state,work) && !state->history_used && !state->exiting) {
        history_add(&state->history,record);
    }
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
    /* Leave the fixed user stack available for nested source/launch frames. */
    int64_t page=uos_alloc_pages(1);
    if (page>0) { state.history.lines=(void *)(uintptr_t)page; }
    int result=0;
    while (!state.exiting) {
        if (uos_getcwd(cwd,sizeof(cwd))<0) { result=1;break; }
        print("user:");print(cwd);print("$ ");
        int64_t n=uos_readline(line,sizeof(line));if (n==UOS_EOF) { break; }
        if (n<0) { state.status=2;print("shell: input rejected; command not executed\n");if (n==UOS_ERROR) { continue; }result=1;break; }
        if (n) { submit_line(&state,line); }
    }
    if (state.history.lines && uos_free_pages(state.history.lines)) { return 1; }
    return result;
}
