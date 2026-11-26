#include "user_timers.h"
#include <assert.h>
#include <stdio.h>

static void clock_cases(void)
{
    int64_t token=user_timer_open(1);
    assert(token>0);
    struct uos_timer_info info={0};
    uint64_t pending=99;
    assert(!user_timer_arm(1,token,100,7,0,false));
    assert(user_timer_read(1,token,106,&pending)==UOS_EAGAIN && pending==99);
    assert(user_timer_arm(1,token,106,0,0,false)==UOS_ERROR);
    assert(!user_timer_info(1,token,107,&info) && !info.deadline_ms && info.pending==1);
    assert(!user_timer_read(1,token,900,&pending) && pending==1);
    assert(user_timer_read(1,token,900,&pending)==UOS_EAGAIN);

    assert(!user_timer_arm(1,token,1000,500,120,true));
    assert(!user_timer_info(1,token,1000,&info) && info.pending==5 && info.deadline_ms==1100);
    assert(!user_timer_info(1,token,1360,&info) && info.pending==8 && info.deadline_ms==1460);
    assert(!user_timer_read(1,token,1360,&pending) && pending==8);
    assert(!user_timer_info(1,token,1360,&info) && !info.pending && info.deadline_ms==1460);
    assert(!user_timer_disarm(1,token));
    assert(!user_timer_info(1,token,9000,&info) && !info.pending && !info.deadline_ms && !info.interval_ms);

    assert(user_timer_arm(1,token,0,UOS_TIMER_MAX_MS+1,0,true)==UOS_ERROR);
    assert(user_timer_arm(1,token,UINT64_MAX-1,2,0,false)==UOS_ERROR);
    assert(!user_timer_arm(1,token,UINT64_MAX-1,1,1,false));
    assert(!user_timer_info(1,token,UINT64_MAX,&info) && info.pending==1 && !info.deadline_ms);
    assert(!user_timer_arm(1,token,UINT64_MAX,1,1,true));
    assert(!user_timer_read(1,token,UINT64_MAX,&pending) && pending==UINT64_MAX);
    assert(!user_timer_close(1,token));
}

static void owner_cases(void)
{
    int64_t tokens[UOS_TIMER_MAX];
    for (unsigned i=0;i<UOS_TIMER_MAX;++i) {
        tokens[i]=user_timer_open(i/UOS_TIMER_OWNER_MAX+1);
        assert(tokens[i]>0);
        if (i==UOS_TIMER_OWNER_MAX-1) { assert(user_timer_open(1)==UOS_ENOSPC); }
    }
    assert(user_timers_count()==UOS_TIMER_MAX && user_timer_open(99)==UOS_ENOSPC);
    struct uos_timer_info info={.pending=37};
    assert(user_timer_info(2,tokens[0],0,&info)==UOS_EBADF && info.pending==37);
    assert(user_timer_disarm(99,tokens[0])==UOS_EBADF);
    assert(user_timer_close(99,tokens[0])==UOS_EBADF);
    user_timers_close_owner(1);
    assert(user_timers_count()==UOS_TIMER_MAX-UOS_TIMER_OWNER_MAX);
    int64_t replacement=user_timer_open(1);
    assert(replacement>0 && replacement!=tokens[0]);
    assert(user_timer_close(1,tokens[0])==UOS_EBADF);
    assert(!user_timer_close(1,replacement) && user_timer_close(1,replacement)==UOS_EBADF);
    for (unsigned owner=2;owner<=UOS_TIMER_MAX/UOS_TIMER_OWNER_MAX;++owner) { user_timers_close_owner(owner); }
    assert(!user_timers_count());
}

int main(void)
{
    clock_cases();
    owner_cases();
    puts("PASS: user timer arithmetic, expiration counts, ownership, quotas and slot reuse.");
}
