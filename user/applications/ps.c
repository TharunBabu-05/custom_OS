/* Process viewer and bounded ownership/lifecycle acceptance tool. */
#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void fill(void *p,size_t n,unsigned char v) { volatile unsigned char *s=p;while (n--) { *s++=v; } }
static int filled(const void *p,size_t n,unsigned char v) { const unsigned char *s=p;while (n--) { if (*s++!=v) { return 0; } }return 1; }
static void digits(char *out,uint64_t n)
{ char text[24];unsigned i=0;do { text[i++]=(char)('0'+n%10);n/=10; }while (n);for (unsigned j=0;j<i;++j) { out[j]=text[i-1-j]; }out[i]=0; }
static void number(uint64_t n) { char text[24];digits(text,n);uos_puts(text); }
static uint64_t value(const char *s) { uint64_t n=0;while (*s>='0' && *s<='9') { n=n*10+(unsigned)(*s++-'0'); }return n; }
static void word(struct app_args *a,const char *s) { a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static void make(struct app_args *a,const char *mode,const char *lease)
{ *a=(struct app_args){0};word(a,"/boot/PS.ELF");word(a,mode);if (lease) { word(a,lease); } }
static const char *state(unsigned s)
{
    switch (s) {
    case UOS_PROCESS_RUNNING:return "running";case UOS_PROCESS_READY:return "ready";case UOS_PROCESS_SLEEPING:return "sleeping";
    case UOS_PROCESS_INPUT:return "input";case UOS_PROCESS_WAIT_CHILD:return "wait-child";case UOS_PROCESS_PIPE_READ:return "pipe-read";
    case UOS_PROCESS_PIPE_WRITE:return "pipe-write";case UOS_PROCESS_EXITED:return "exited";case UOS_PROCESS_FAULTED:return "faulted";
    case UOS_PROCESS_TIMEOUT:return "timeout";case UOS_PROCESS_CANCELLED:return "cancelled";default:return "invalid";
    }
}
static int valid(const struct uos_process_table *t)
{
    if (t->version!=1 || !t->count || t->count>UOS_PROCESS_MAX || t->caller_pid!=(uint64_t)uos_getpid()) { return 0; }unsigned self=0;
    for (unsigned i=0;i<UOS_PROCESS_MAX;++i) {
        const struct uos_process_entry *e=&t->entries[i];
        if (i>=t->count) { if (!filled(e,sizeof(*e),0)) { return 0; }continue; }
        if (!e->pid || e->state<UOS_PROCESS_RUNNING || e->state>UOS_PROCESS_CANCELLED || e->flags&~3u || e->reserved || e->heap_pages>16) { return 0; }
        for (unsigned j=0;j<i;++j) { if (t->entries[j].pid==e->pid) { return 0; } }
        if (e->flags&UOS_PROCESS_SELF) { if (e->pid!=t->caller_pid || e->state!=UOS_PROCESS_RUNNING) { return 0; }++self; }
        else if (e->pid==t->caller_pid || e->state==UOS_PROCESS_RUNNING) { return 0; }
        if ((e->flags&UOS_PROCESS_CHILD) && e->parent_pid!=t->caller_pid) { return 0; }
        if (e->state<UOS_PROCESS_EXITED && e->exit_code) { return 0; }
        size_t n=0;while (n<sizeof(e->path) && e->path[n]) { ++n; }
        if (!n || n==sizeof(e->path) || !filled(e->path+n,sizeof(e->path)-n,0)) { return 0; }
    }return self==1;
}
static const struct uos_process_entry *find(const struct uos_process_table *t,uint64_t pid)
{ for (unsigned i=0;i<t->count;++i) { if (t->entries[i].pid==pid) { return &t->entries[i]; } }return NULL; }
static int snapshot(struct uos_process_table *t) { return !uos_processes(t) && valid(t); }
static int show(void)
{
    struct uos_process_table t;if (!snapshot(&t)) { uos_puts("ps: snapshot failed\n");return 1; }
    uos_puts("PID PPID STATE PAGES PATH\n");
    for (unsigned i=0;i<t.count;++i) {
        const struct uos_process_entry *e=&t.entries[i];number(e->pid);uos_puts(" ");number(e->parent_pid);uos_puts(" ");
        uos_puts(state(e->state));uos_puts(" ");number(e->heap_pages);uos_puts(" ");
        char safe[256];unsigned at=0;do { unsigned char c=(unsigned char)e->path[at];safe[at]=c>=32 && c<127 ? (char)c : c ? '.' : 0; }while (e->path[at++]);
        uos_puts(safe);uos_puts("\n");
    }return 0;
}
static int until(uint64_t pid,unsigned wanted,struct uos_child_info *out)
{
    int64_t begin=uos_time_ms();
    do {
        if (uos_child_poll(pid,out) || out->pid!=pid || out->reserved || out->finished>1 ||
            (out->state<UOS_PROCESS_EXITED && (out->finished || out->exit_code)) ||
            (out->state>=UOS_PROCESS_EXITED && !out->finished)) { return 0; }
        if (wanted ? out->state==wanted : out->finished) { return 1; }
        if (uos_sleep_ms(10)) { return 0; }
    }while (uos_time_ms()-begin<5000);return 0;
}
static int foreign(void)
{
    struct uos_process_table t;struct uos_child_info info;
    for (unsigned round=0;round<12;++round) {
        if (!snapshot(&t) || t.count!=3) { return 40; }
        const struct uos_process_entry *self=find(&t,t.caller_pid);if (!self || !self->parent_pid) { return 41; }
        const struct uos_process_entry *parent=find(&t,self->parent_pid);
        if (!parent || parent->state!=UOS_PROCESS_WAIT_CHILD) { return 42; }
        for (unsigned i=0;i<t.count;++i) {
            if (uos_child_poll(t.entries[i].pid,&info)!=UOS_ECHILD || uos_child_cancel(t.entries[i].pid)!=UOS_ECHILD) { return 43; }
        }
        if (uos_yield() || uos_sleep_ms(10)) { return 44; }
    }
    char bytes[16];int64_t n=uos_stdin(bytes,sizeof(bytes));
    if (n==UOS_ENOTTY) { return uos_puts("peer-ok\n")==8 ? 0 : 45; }
    unsigned count=0;
    while (n>0) { for (int64_t i=0;i<n;++i) { if (count>=8 || bytes[i]!="peer-ok\n"[count++]) { return 46; } }n=uos_stdin(bytes,sizeof(bytes)); }
    return n==0 && count==8 ? 0 : 47;
}
static int worker(const char *mode)
{
    char lease[256],text[24];struct uos_process_table t;struct uos_child_info info;
    if (uos_arg(2,lease,sizeof(lease))<0 || !snapshot(&t)) { return 50; }
    const struct uos_process_entry *self=find(&t,t.caller_pid);
    if (!self || !self->parent_pid || uos_child_cancel(self->parent_pid)!=UOS_ECHILD ||
        uos_child_poll(self->parent_pid,&info)!=UOS_ECHILD || uos_child_cancel(t.caller_pid)!=UOS_ECHILD) { return 51; }
    if (same(mode,"after")) {
        struct uos_memory_info memory;
        return uos_arg(3,text,sizeof(text))>=0 && value(text)==t.caller_pid && !uos_memory_info(&memory) && !memory.live_pages ? 37 : 52;
    }
    int64_t address=uos_alloc_pages(2),handle=uos_open(lease,UOS_READ);if (address<0 || handle<0) { return 53; }
    unsigned char *pages=(void *)(uintptr_t)address;fill(pages,8192,0x6c);
    if (same(mode,"done")) { return 37; }
    if (same(mode,"fault")) { return *(volatile unsigned char *)(uintptr_t)0; }
    if (same(mode,"exec")) { struct app_args args;make(&args,"after",lease);digits(text,t.caller_pid);word(&args,text);(void)uos_exec(&args);return 54; }
    if (same(mode,"input")) { return uos_readline(text,sizeof(text))==8 && same(text,"child-ok") ? 37 : 55; }
    for (;;) { if (!filled(pages,8192,0x6c) || uos_sleep_ms(100)) { return 56; } }
}
static int checks(void)
{
    struct uos_process_table t;struct uos_child_info info;
    if (!snapshot(&t) || find(&t,t.caller_pid)->parent_pid) { uos_puts("ps check requires appterm at the kernel monitor.\n");return 1; }
    uint64_t own=t.caller_pid;fill(&t,sizeof(t),0xa7);
    if (uos_call(UOS_SYS_PROCESSES,(uintptr_t)&t,sizeof(t)-1,0,0,0,0)!=UOS_ERROR || !filled(&t,sizeof(t),0xa7) ||
        uos_call(UOS_SYS_PROCESSES,(uintptr_t)&t,sizeof(t)+1,0,0,0,0)!=UOS_ERROR || !filled(&t,sizeof(t),0xa7) ||
        uos_processes(NULL)!=UOS_EFAULT || uos_processes((void *)(uintptr_t)UINT64_MAX)!=UOS_EFAULT ||
        uos_processes((void *)(uintptr_t)"readonly")!=UOS_EFAULT) { return 2; }
    int64_t allocation=uos_alloc_pages(1);if (allocation<0) { return 3; }unsigned char *page=(void *)(uintptr_t)allocation;
    fill(page+4080,16,0x7c);
    if (uos_processes((void *)(page+4080))!=UOS_EFAULT || !filled(page+4080,16,0x7c) || uos_free_pages(page) ||
        uos_processes((void *)page)!=UOS_EFAULT) { return 4; }
    if (uos_child_cancel(0)!=UOS_ECHILD || uos_child_cancel(own)!=UOS_ECHILD ||
        uos_child_cancel(UINT64_MAX)!=UOS_ECHILD || uos_child_poll(own,&info)!=UOS_ECHILD) { return 5; }
    char lease[64]="/tmp/.proc-",id[24];digits(id,own);unsigned at=11;
    for (unsigned i=0;;++i) { lease[at++]=id[i];if (!id[i]) { break; } }
    struct uos_entry entry;if (uos_stat(lease,&entry)!=UOS_ERROR || uos_replace(lease,"lease",5)!=5) { return 6; }
    struct app_args args;uint64_t previous=0;
    for (unsigned round=0;round<6;++round) {
        make(&args,round&1 ? "input" : "worker",lease);int64_t pid=uos_spawn(&args,30);if (pid<=0 || (uint64_t)pid<=previous) { return 7; }
        if (!until((uint64_t)pid,round&1 ? UOS_PROCESS_INPUT : UOS_PROCESS_SLEEPING,&info) || info.finished ||
            uos_child_poll((uint64_t)pid,NULL)!=UOS_EFAULT || uos_child_poll((uint64_t)pid,(void *)(uintptr_t)"readonly")!=UOS_EFAULT ||
            uos_child_poll((uint64_t)pid,(void *)(uintptr_t)UINT64_MAX)!=UOS_EFAULT || uos_child_cancel(previous)!=UOS_ECHILD) { return 8; }
        if (!round) {
            allocation=uos_alloc_pages(1);if (allocation<0) { return 15; }page=(void *)(uintptr_t)allocation;fill(page+4088,8,0x71);
            if (uos_child_poll((uint64_t)pid,(void *)(page+4088))!=UOS_EFAULT || !filled(page+4088,8,0x71) ||
                uos_free_pages(page) || uos_child_poll((uint64_t)pid,(void *)page)!=UOS_EFAULT) { return 16; }
        }
        if (!snapshot(&t)) { return 9; }const struct uos_process_entry *child=find(&t,(uint64_t)pid);
        if (!child || child->parent_pid!=own || child->flags!=UOS_PROCESS_CHILD || child->heap_pages!=2 ||
            uos_replace(lease,"blocked",7)!=UOS_ERROR || uos_child_cancel((uint64_t)pid) ||
            !until((uint64_t)pid,UOS_PROCESS_CANCELLED,&info) || !info.finished || info.exit_code!=130 ||
            uos_child_cancel((uint64_t)pid) || uos_child_poll((uint64_t)pid,&info) || info.exit_code!=130 ||
            uos_spawn(&args,1)!=UOS_ERROR || uos_replace(lease,"blocked",7)!=UOS_ERROR) { return 10; }
        if (uos_wait((uint64_t)pid)!=130 || uos_child_poll((uint64_t)pid,&info)!=UOS_ECHILD ||
            uos_child_cancel((uint64_t)pid)!=UOS_ECHILD || uos_wait((uint64_t)pid)!=UOS_ERROR || uos_replace(lease,"lease",5)!=5) { return 11; }
        previous=(uint64_t)pid;
    }
    const char *modes[]={"done","fault","exec","worker"};const int codes[]={37,125,37,124};
    const unsigned states[]={UOS_PROCESS_EXITED,UOS_PROCESS_FAULTED,UOS_PROCESS_EXITED,UOS_PROCESS_TIMEOUT};
    for (unsigned i=0;i<4;++i) {
        make(&args,modes[i],lease);int64_t pid=uos_spawn(&args,i==3 ? 1 : 10);
        if (pid<=0 || !until((uint64_t)pid,0,&info) || info.state!=states[i] || info.exit_code!=codes[i] ||
            uos_child_cancel((uint64_t)pid) || uos_child_poll((uint64_t)pid,&info) || info.exit_code!=codes[i] ||
            uos_wait((uint64_t)pid)!=codes[i]) { return 12; }
    }
    if (uos_remove(lease,0)) { return 13; }
    struct app_args right;make(&args,"foreign",NULL);make(&right,"foreign",NULL);
    if (uos_pipeline(&args,&right,5,NULL)) { return 14; }
    uos_puts("PASS: process snapshots, checked buffers, nonblocking poll, parent-only cancellation, stale PIDs, preserved exit/fault/exec/timeout results, leases and repeated cleanup.\n");
    uos_puts("PASS: three-slot parent/sibling snapshots and foreign PID refusal.\n");return 0;
}
static int terminal_check(void)
{
    struct app_args args;struct uos_child_info info;char text[24];make(&args,"input","/boot/CONFIG.TXT");int64_t pid=uos_spawn(&args,10);
    if (pid<0 || !until((uint64_t)pid,UOS_PROCESS_INPUT,&info) || uos_child_cancel((uint64_t)pid) || uos_wait((uint64_t)pid)!=130) { return 60; }
    uos_puts("PSCANCEL: enter parent-ok\n");if (uos_readline(text,sizeof(text))!=9 || !same(text,"parent-ok")) { return 61; }
    pid=uos_spawn(&args,10);if (pid<0 || !until((uint64_t)pid,UOS_PROCESS_INPUT,&info)) { return 62; }
    uos_puts("Now hand terminal to child; enter child-ok\n");if (uos_wait((uint64_t)pid)!=37) { return 63; }
    uos_puts("PSCANCEL: enter root-ok\n");if (uos_readline(text,sizeof(text))!=7 || !same(text,"root-ok")) { return 64; }
    uos_puts("PASS: cancelling a pending child reader preserves parent input and later terminal handoff.\n");return 0;
}
static int demo(void)
{
    struct app_args args;make(&args,"worker","/boot/CONFIG.TXT");int64_t pid=uos_spawn(&args,30);
    if (pid<0) { uos_puts("ps demo requires appterm at the kernel monitor.\n");return 1; }
    uos_puts("Bounded child started. Commands: status, stop, wait, start, exit.\n");char line[24];
    for (;;) {
        uos_puts("process> ");if (uos_readline(line,sizeof(line))<0 || same(line,"exit")) { return 0; }
        if (same(line,"status")) {
            if (show()) { return 2; }if (pid<0) { uos_puts("No outstanding child.\n");continue; }
            struct uos_child_info info;if (uos_child_poll((uint64_t)pid,&info)) { return 3; }
            uos_puts("Child PID=");number((uint64_t)pid);uos_puts(" state=");uos_puts(state(info.state));uos_puts(" finished=");number(info.finished);uos_puts("\n");
        } else if (same(line,"start")) {
            if (pid>=0) { uos_puts("Collect the existing child first.\n");continue; }
            pid=uos_spawn(&args,30);if (pid<0) { return 4; }uos_puts("New child PID=");number((uint64_t)pid);uos_puts("\n");
        } else if (same(line,"stop") || same(line,"wait")) {
            if (pid<0) { uos_puts("No outstanding child.\n");continue; }
            if (same(line,"stop") && uos_child_cancel((uint64_t)pid)) { return 5; }
            int64_t code=uos_wait((uint64_t)pid);if (code<0) { return 6; }pid=-1;uos_puts("Child collected: exit=");number((uint64_t)code);uos_puts("\n");
        } else { uos_puts("Use status, stop, wait, start or exit.\n"); }
    }
}
int app_main(void)
{
    if (uos_argc()==1) { return show(); }char mode[24];if (uos_arg(1,mode,sizeof(mode))<0) { return 2; }
    if (same(mode,"check")) { return checks(); }if (same(mode,"demo")) { return demo(); }
    if (same(mode,"terminal")) { return terminal_check(); }if (same(mode,"foreign")) { return foreign(); }
    if (same(mode,"orphan")) {
        struct app_args args;make(&args,"worker","/boot/CONFIG.TXT");int64_t pid=uos_spawn(&args,30);struct uos_child_info info;
        if (pid<0 || !until((uint64_t)pid,UOS_PROCESS_SLEEPING,&info)) { return 3; }
        uos_puts("Parent exits with an outstanding owned child.\n");return 0;
    }
    if (same(mode,"worker") || same(mode,"input") || same(mode,"done") || same(mode,"fault") || same(mode,"exec") || same(mode,"after")) { return worker(mode); }
    uos_puts("Usage: ps [check|demo]\n");return 2;
}
