#include "user.h"

static const char app[]="/boot/SCHEDCHK.ELF";

static void number(uint64_t value)
{
    char text[21];
    unsigned at=sizeof(text)-1;
    text[at]=0;
    do { text[--at]=(char)('0'+value%10);value/=10; } while (value);
    uos_puts(text+at);
}

static int failed(unsigned line)
{
    uos_puts("FAIL: SCHEDCHK line ");number(line);uos_puts("\n");
    return 1;
}
#define CHECK(condition) do { if (!(condition)) { return failed(__LINE__); } } while (0)

static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}

static int64_t spawn(const char *path,const char *mode)
{
    struct app_args args={0};
    word(&args,path);
    if (mode) { word(&args,mode); }
    return uos_spawn(&args,8);
}

static int compare(uint64_t low,uint64_t high)
{
    struct uos_sched_info start_low,start_high,end_low,end_high;
    CHECK(!uos_sched_get(low,&start_low) && start_low.quantum_ticks==UOS_SCHED_LOW);
    CHECK(!uos_sched_get(high,&start_high) && start_high.quantum_ticks==UOS_SCHED_HIGH);
    CHECK(!uos_sleep_ms(700) && !uos_sleep_ms(700));
    CHECK(!uos_sched_get(low,&end_low) && !uos_sched_get(high,&end_high));
    uint64_t low_ticks=end_low.user_ticks-start_low.user_ticks;
    uint64_t high_ticks=end_high.user_ticks-start_high.user_ticks;
    uos_puts("User ticks low=");number(low_ticks);uos_puts(" high=");number(high_ticks);uos_puts("\n");
    CHECK(low_ticks>=8 && high_ticks>=3*low_ticks && high_ticks<=5*low_ticks);
    CHECK(end_low.ready_ticks-start_low.ready_ticks>=2*low_ticks && end_high.ready_ticks>start_high.ready_ticks);
    return 0;
}

static int inherited(void)
{
    CHECK(uos_signal_wait(3000)==UOS_SIGUSR1);
    struct uos_sched_info info;
    do {
        CHECK(!uos_sched_get(0,&info) && info.quantum_ticks==UOS_SCHED_HIGH);
    } while (info.user_ticks<8);
    int64_t pid=spawn(app,"descendant");
    CHECK(pid>0 && !uos_wait((uint64_t)pid));
    struct app_args next={0};
    word(&next,app);word(&next,"exec");
    (void)uos_exec(&next);
    return failed(__LINE__);
}

int app_main(void)
{
    struct uos_sched_info info;
    CHECK(!uos_sched_get(0,&info) && info.pid==(uint64_t)uos_getpid() && !info.reserved);
    if (uos_argc()==2) {
        char mode[16];
        CHECK(uos_arg(1,mode,sizeof(mode))>0);
        if (mode[0]=='i') { return inherited(); }
        CHECK(info.quantum_ticks==UOS_SCHED_HIGH);
        if (mode[0]=='d') { return 0; }
        CHECK(mode[0]=='e' && info.user_ticks>=8);
        return 0;
    }
    CHECK(uos_argc()==1 && info.quantum_ticks==UOS_SCHED_NORMAL);
    CHECK(uos_sched_get(0,0)==UOS_EFAULT && uos_sched_get(info.pid,&info)==UOS_ECHILD &&
          uos_sched_set(info.pid,UOS_SCHED_HIGH)==UOS_ECHILD);
    int64_t first=spawn("/boot/SPIN.ELF",0);
    int64_t second=spawn("/boot/SPIN.ELF",0);
    CHECK(first>0 && second>first);
    uint64_t a=(uint64_t)first,b=(uint64_t)second;
    CHECK(uos_call2(UOS_SYS_SCHED_SET,a,(1ULL<<32)|UOS_SCHED_HIGH)==UOS_ERROR);
    CHECK(!uos_sched_get(a,&info) && info.quantum_ticks==UOS_SCHED_NORMAL && info.pid==a);
    CHECK(!uos_sched_set(a,UOS_SCHED_LOW) && !uos_sched_set(b,UOS_SCHED_HIGH) && !compare(a,b));
    CHECK(!uos_sched_set(a,UOS_SCHED_HIGH) && !uos_sched_set(b,UOS_SCHED_LOW) && !compare(b,a));
    CHECK(!uos_child_cancel(a) && !uos_child_cancel(b));
    CHECK(!uos_sched_get(a,&info) && info.quantum_ticks==UOS_SCHED_HIGH && info.user_ticks &&
          uos_sched_set(a,UOS_SCHED_NORMAL)==UOS_ECHILD);
    CHECK(uos_wait(a)==130 && uos_wait(b)==130 && uos_sched_get(a,&info)==UOS_ECHILD);

    int64_t child=spawn(app,"inherit");
    CHECK(child>0 && !uos_sched_set((uint64_t)child,UOS_SCHED_HIGH) &&
          !uos_signal_send((uint64_t)child,UOS_SIGUSR1) && !uos_wait((uint64_t)child));
    uos_puts("PASS: weighted EL0 scheduling, bounded progress, priority ownership, inheritance and exec accounting.\n");
    return 0;
}
