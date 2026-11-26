#include "text_io.h"

static const char app[]="/boot/TMRCHK.ELF";
enum { WAIT_MS=1200 };

static bool same(const char *left,const char *right)
{
    while (*left && *left==*right) { ++left; ++right; }
    return *left==*right;
}

static char *decimal(char text[21],uint64_t n)
{
    unsigned at=20;
    text[at]=0;
    do { text[--at]=(char)('0'+n%10);n/=10; } while (n);
    return text+at;
}

static int failure(unsigned line)
{
    char text[21];
    uos_puts("FAIL: TMRCHK line ");
    uos_puts(decimal(text,line));
    uos_puts("\n");
    return 1;
}

#define CHECK(test) do { if (!(test)) { return failure(__LINE__); } } while (0)

static void word(struct app_args *args,const char *text)
{
    args->offset[args->count++]=args->used;
    do { args->bytes[args->used++]=*text; } while (*text++);
}

static struct app_args token_args(const char *path,const char *mode,uint64_t token)
{
    struct app_args args={0};
    char digits[21];
    word(&args,path);
    word(&args,mode);
    word(&args,decimal(digits,token));
    return args;
}

static struct uos_event timer_event(uint64_t timer)
{
    return (struct uos_event){.object=timer,.source=UOS_EVENT_TIMER,.events=UOS_EVENT_READ};
}

static bool wait_timer(uint64_t timer,unsigned timeout,unsigned expected)
{
    struct uos_event event=timer_event(timer);
    int64_t n=uos_event_wait(&event,1,timeout);
    return n==(expected!=0) && event.revents==expected;
}

static int ownership(uint64_t timer)
{
    uint64_t count=99;
    struct uos_timer_info info;
    CHECK(uos_timer_info(timer,&info)==UOS_EBADF);
    CHECK(uos_timer_arm(timer,20,0)==UOS_EBADF);
    CHECK(uos_timer_disarm(timer)==UOS_EBADF && uos_timer_close(timer)==UOS_EBADF);
    CHECK(uos_timer_read(timer,&count)==UOS_EBADF && count==99);
    CHECK(wait_timer(timer,0,UOS_EVENT_HANGUP));
    int64_t personal=uos_timer_open();
    CHECK(personal>0 && (uint64_t)personal!=timer);
    CHECK(!uos_timer_arm((uint64_t)personal,1000,0));
    return 0;
}

static int lifecycle(uint64_t timer)
{
    struct app_args child=token_args(app,"foreign",timer);
    int64_t pid=uos_spawn(&child,5);
    CHECK(pid>0);
    struct uos_event events[]={
        timer_event(timer),{.object=(uint64_t)pid,.source=UOS_EVENT_CHILD,.events=UOS_EVENT_READ}
    };
    int64_t n=uos_event_wait(events,2,WAIT_MS);
    CHECK(n==1 && !events[0].revents && events[1].revents==UOS_EVENT_READ);
    CHECK(!uos_wait((uint64_t)pid));

    CHECK(!uos_timer_arm(timer,1000,0));
    struct app_args missing=token_args("/boot/NOAPP.ELF","exec",timer);
    struct uos_timer_info info;
    CHECK(uos_exec(&missing)<0 && !uos_timer_info(timer,&info));
    struct app_args next=token_args(app,"after-exec",timer);
    CHECK(!uos_exec(&next));
    return 1;
}

static int check(void)
{
    uint64_t handles[UOS_TIMER_OWNER_MAX];
    for (unsigned i=0;i<UOS_TIMER_OWNER_MAX;++i) {
        int64_t h=uos_timer_open();
        CHECK(h>0);
        handles[i]=(uint64_t)h;
    }
    CHECK(uos_timer_open()==UOS_ENOSPC);
    for (unsigned i=1;i<UOS_TIMER_OWNER_MAX;++i) { CHECK(!uos_timer_close(handles[i])); }
    uint64_t timer=handles[0],count=0;
    struct uos_timer_info info;
    CHECK(!uos_timer_info(timer,&info) && !info.deadline_ms && !info.pending);
    CHECK(uos_timer_read(timer,&count)==UOS_EAGAIN);
    CHECK(uos_timer_arm(timer,0,0)==UOS_ERROR && uos_timer_arm(timer,1,UOS_TIMER_MAX_MS+1)==UOS_ERROR);
    CHECK(wait_timer(handles[1],0,UOS_EVENT_HANGUP));
    struct uos_event invalid=timer_event(timer);
    invalid.events=UOS_EVENT_WRITE;
    CHECK(uos_event_wait(&invalid,1,0)==UOS_ERROR);

    int64_t before=uos_time_ms();
    CHECK(!uos_timer_arm(timer,50,0) && wait_timer(timer,WAIT_MS,UOS_EVENT_READ));
    CHECK(uos_time_ms()-before>=50);
    CHECK(uos_timer_read(timer,NULL)==UOS_EFAULT && uos_timer_info(timer,NULL)==UOS_EFAULT);
    CHECK(wait_timer(timer,0,UOS_EVENT_READ) && !uos_timer_info(timer,&info) && info.pending==1 && !info.deadline_ms);
    CHECK(!uos_timer_read(timer,&count) && count==1 && wait_timer(timer,0,0));

    uint64_t now=(uint64_t)uos_time_ms();
    uint64_t past=now>120 ? now-120 : 1;
    CHECK(!uos_timer_arm_at(timer,past,20));
    CHECK(!uos_timer_info(timer,&info) && info.pending>=1 && info.interval_ms==20 &&
          info.deadline_ms>=past && !((info.deadline_ms-past)%20));
    if (now>120) { CHECK(info.pending>=7); }
    CHECK(!uos_timer_read(timer,&count) && count>=info.pending);
    CHECK(!uos_timer_arm_at(timer,(uint64_t)uos_time_ms(),0) && wait_timer(timer,0,UOS_EVENT_READ));
    CHECK(!uos_timer_arm(timer,1000,0) && wait_timer(timer,0,0));
    CHECK(!uos_timer_disarm(timer) && wait_timer(timer,30,0));
    CHECK(!uos_timer_info(timer,&info) && !info.pending && !info.deadline_ms && !info.interval_ms);
    return lifecycle(timer);
}

static int once(unsigned ms)
{
    int64_t handle=uos_timer_open(),start=uos_time_ms();
    uint64_t count;
    CHECK(handle>0 && !uos_timer_arm((uint64_t)handle,ms,0));
    CHECK(wait_timer((uint64_t)handle,WAIT_MS+ms,UOS_EVENT_READ));
    CHECK(!uos_timer_read((uint64_t)handle,&count) && count==1 && !uos_timer_close((uint64_t)handle));
    char digits[21];
    uos_puts("timer fired elapsed_ms=");
    uos_puts(decimal(digits,(uint64_t)(uos_time_ms()-start)));
    uos_puts("\n");
    return 0;
}

static bool parse_token(const char *text,uint64_t *token)
{
    uint64_t value=0;
    if (!*text) { return false; }
    for (;*text;++text) {
        if (*text<'0' || *text>'9') { return false; }
        unsigned digit=(unsigned)(*text-'0');
        if (value>(UINT64_MAX-digit)/10) { return false; }
        value=value*10+digit;
    }
    *token=value;
    return value!=0;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char mode[16],arg[24];
    if (argc==1) { return check(); }
    if (argc>=2 && uos_arg(1,mode,sizeof(mode))>0) {
        if (argc==2 && (same(mode,"hold") || same(mode,"fault"))) {
            int64_t timer=uos_timer_open();
            CHECK(timer>0 && !uos_timer_arm((uint64_t)timer,1000,1000));
            if (same(mode,"fault")) { __asm__ volatile("brk #0x50"); }
            for (;;) { CHECK(!uos_sleep_ms(1000)); }
        }
        if (argc==3 && uos_arg(2,arg,sizeof(arg))>0) {
            unsigned ms;
            uint64_t token;
            if (same(mode,"once") && text_count(arg,1000,&ms) && ms) { return once(ms); }
            if (parse_token(arg,&token)) {
                if (same(mode,"foreign")) { return ownership(token); }
                if (same(mode,"after-exec")) {
                    struct uos_timer_info info;
                    CHECK(uos_timer_info(token,&info)==UOS_EBADF);
                    uos_puts("PASS: user timers, event waits, ownership and exec cleanup.\n");
                    return 0;
                }
            }
        }
    }
    uos_puts("Usage: tmrchk [once <1..1000>]\n");
    return 2;
}
