#include "user.h"

static const char app[]="/boot/TREECHK.ELF";

static int failed(unsigned line)
{
    char number[12];
    unsigned at=sizeof(number)-1;
    number[at]=0;
    do { number[--at]=(char)('0'+line%10);line/=10; } while (line);
    uos_puts("FAIL: TREECHK line ");uos_puts(number+at);uos_puts("\n");
    return 1;
}
#define CHECK(condition) do { if (!(condition)) { return failed(__LINE__); } } while (0)

static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}

static int64_t spawn(const char *mode,char scenario,unsigned seconds)
{
    struct app_args args={0};
    word(&args,app);word(&args,mode);
    if (scenario) { char text[]={scenario,0};word(&args,text); }
    return uos_spawn(&args,seconds);
}

static unsigned find(const struct uos_process_table_v2 *table,uint64_t pid)
{
    for (unsigned i=0;i<table->count;++i) { if (table->entries[i].pid==pid) { return i; } }
    return table->count;
}

static uint64_t parent_pid(void)
{
    struct uos_process_table_v2 table;
    if (uos_processes_v2(&table) || table.count>UOS_PROCESS_MAX_V2) { return 0; }
    unsigned self=find(&table,table.caller_pid);
    return self<table.count ? table.entries[self].parent_pid : 0;
}

static int64_t send(uint64_t peer,uint64_t value)
{
    struct uos_message message={.peer=peer,.bytes=8};
    for (unsigned i=0;i<8;++i) { message.data[i]=(unsigned char)(value>>(8*i)); }
    return uos_ipc_send(&message,2000);
}

static uint64_t receive(uint64_t peer)
{
    struct uos_message message;
    if (uos_ipc_recv(peer,&message,2000) || message.peer!=peer || message.bytes!=8 || message.handle) { return 0; }
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) { value|=(uint64_t)message.data[i]<<(8*i); }
    return value;
}

static int worker(void)
{
    uint64_t parent=parent_pid();
    CHECK(parent && uos_alloc_pages(2)>0 && uos_open("/etc/hostname",UOS_READ)>0);
    CHECK(!send(parent,1) && uos_signal_wait(8000)==UOS_SIGUSR1);
    return 19;
}

static int leaf(void)
{
    uint64_t parent=parent_pid();
    int64_t child=spawn("worker",0,10);
    CHECK(parent && child>0 && receive((uint64_t)child)==1 && !send(parent,(uint64_t)child));
    CHECK(uos_signal_wait(8000)==UOS_SIGUSR1);
    CHECK(!uos_signal_send((uint64_t)child,UOS_SIGUSR1) && uos_wait((uint64_t)child)==19);
    return 23;
}

static int branch(char scenario)
{
    uint64_t parent=parent_pid();
    CHECK(parent && receive(parent)==1);
    int64_t child=spawn("leaf",0,10);
    CHECK(child>0 && receive((uint64_t)child)>0 && !send(parent,(uint64_t)child));
    if (scenario=='c' || scenario=='t') {
        /* The parent cancels this wait, or this process reaches its deadline. */
        (void)uos_wait((uint64_t)child);
        return failed(__LINE__);
    }
    CHECK(receive(parent)==1);
    if (scenario=='w') {
        CHECK(!uos_signal_send((uint64_t)child,UOS_SIGUSR1) && uos_wait((uint64_t)child)==23);
    }
    if (scenario=='f') { __asm__ volatile("brk #0"); }
    return 37;
}

static int check(char scenario,int expected,uint64_t *previous)
{
    /* Occupy a lower slot until the branch exists, then reuse it for the
     * branch's child. Teardown must follow parent links, regardless of index. */
    int64_t holder=spawn("quick",0,5),middle=spawn("branch",scenario,scenario=='t' ? 2 : 8);
    CHECK(holder>0 && middle>holder && (uint64_t)holder>*previous);
    uint64_t branch_pid=(uint64_t)middle;
    CHECK(!uos_wait((uint64_t)holder) && !send(branch_pid,1));
    uint64_t leaf_pid=receive(branch_pid);
    CHECK(leaf_pid>branch_pid);
    struct uos_process_table_v2 table;
    CHECK(!uos_processes_v2(&table) && table.count<=UOS_PROCESS_MAX_V2);
    unsigned b=find(&table,branch_pid),l=find(&table,leaf_pid);
    CHECK(l<b && b<table.count && table.entries[b].parent_pid==table.caller_pid &&
          (table.entries[b].flags&UOS_PROCESS_CHILD) && table.entries[l].parent_pid==branch_pid && !table.entries[l].flags);
    uint64_t worker_pid=0;
    for (unsigned i=0;i<table.count;++i) {
        if (table.entries[i].parent_pid==leaf_pid) { worker_pid=table.entries[i].pid; }
    }
    CHECK(worker_pid>leaf_pid);
    struct uos_child_info info;
    CHECK(uos_child_poll(leaf_pid,&info)==UOS_ECHILD && uos_child_cancel(worker_pid)==UOS_ECHILD &&
          uos_signal_send(leaf_pid,UOS_SIGTERM)==UOS_ECHILD && send(leaf_pid,1)==UOS_EPIPE && uos_wait(leaf_pid)==UOS_ERROR);

    if (scenario=='c') { CHECK(!uos_child_cancel(branch_pid)); }
    else if (scenario!='t') { CHECK(!send(branch_pid,1)); }
    CHECK(uos_wait(branch_pid)==expected && uos_wait(branch_pid)==UOS_ERROR);
    CHECK(!uos_processes_v2(&table) && table.count<=UOS_PROCESS_MAX_V2 && find(&table,branch_pid)==table.count &&
          find(&table,leaf_pid)==table.count && find(&table,worker_pid)==table.count);
    *previous=worker_pid;
    return 0;
}

int app_main(void)
{
    char mode[16];
    if (uos_argc()>1) {
        CHECK(uos_arg(1,mode,sizeof(mode))>0);
        switch (mode[0]) {
        case 'q':return 0;
        case 'w':return worker();
        case 'l':return leaf();
        case 'b': {
            char scenario[2];
            CHECK(uos_arg(2,scenario,sizeof(scenario))==1);
            return branch(scenario[0]);
        }
        case 'd': {
            char line[16];
            CHECK(uos_readline(line,sizeof(line))==UOS_ENOTTY);
            return 0;
        }
        case 'i': {
            int64_t child=spawn("denied",0,3);
            CHECK(child>0 && !uos_wait((uint64_t)child));
            uos_puts("PASS: input redirection bounds terminal delegation.\n");return 0;
        }
        default:return failed(__LINE__);
        }
    }
    uint64_t previous=0;
    if (check('w',37,&previous) || check('e',37,&previous) || check('c',130,&previous) ||
        check('f',125,&previous) || check('t',124,&previous)) { return 1; }
    uos_puts("PASS: nested process ownership, wait, slot reuse and subtree cleanup.\n");
    return 0;
}
