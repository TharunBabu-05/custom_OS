#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "task.h"
static void boundaries(void)
{
    struct task_queue q;task_queue_init(&q);assert(task_queue_done(&q));unsigned id=99;
    assert(task_add(NULL,&id)==TASK_ARGUMENT && id==99);assert(task_add(&q,NULL)==TASK_ARGUMENT);
    assert(task_yield_policy(&q)==TASK_STATE && task_sleep_policy(&q,1,1)==TASK_STATE && task_exit_policy(&q,0)==TASK_STATE);
    for (unsigned i=0;i<TASK_LIMIT;++i) { assert(task_add(&q,&id)==TASK_OK && id==i); }
    struct task_queue saved=q;id=99;assert(task_add(&q,&id)==TASK_FULL && id==99 && !memcmp(&q,&saved,sizeof(q)));
    for (unsigned round=0;round<20;++round) { assert(task_pick(&q,0)==(int)(round%TASK_LIMIT));assert(task_pick(&q,0)==-1);assert(task_yield_policy(&q)==TASK_OK); }
    assert(task_pick(&q,100)==0);saved=q;assert(task_sleep_policy(&q,UINT64_MAX-10,11)==TASK_OVERFLOW && !memcmp(&saved,&q,sizeof(q)));
    assert(task_sleep_policy(&q,100,10)==TASK_OK);assert(task_pick(&q,101)==1);assert(task_sleep_policy(&q,101,20)==TASK_OK);
    assert(task_pick(&q,102)==2 && task_exit_policy(&q,42)==TASK_OK);assert(task_pick(&q,103)==3 && task_exit_policy(&q,0)==TASK_OK);
    assert(task_pick(&q,109)==-1 && !task_queue_done(&q));assert(task_pick(&q,110)==0 && q.tasks[0].wakes==1 && task_sleep_policy(&q,110,0)==TASK_OK);
    assert(task_pick(&q,110)==0 && task_exit_policy(&q,0)==TASK_OK);assert(task_pick(&q,120)==-1);assert(task_pick(&q,121)==1 && task_exit_policy(&q,0)==TASK_OK);
    assert(task_queue_done(&q) && q.tasks[2].exit_code==42 && q.switches==27);
    assert(task_add(&q,&id)==TASK_FULL); /* exited IDs retained until explicit queue reset */
    task_queue_init(&q);assert(task_add(&q,&id)==TASK_OK && task_pick(&q,0)==0);assert(task_sleep_policy(&q,0,10)==TASK_OK);
    assert(task_add(&q,&id)==TASK_OK);task_cancel_all(&q);assert(task_queue_done(&q) && q.tasks[0].status==TASK_CANCELLED && q.tasks[1].status==TASK_CANCELLED && task_pick(&q,100)==-1);
    task_queue_init(&q);assert(task_add(&q,&id)==TASK_OK);q.switches=UINT64_MAX;saved=q;assert(task_pick(&q,0)==-1 && !memcmp(&q,&saved,sizeof(q)));
    puts("PASS: task limits/round-robin fairness, explicit yield, timed sleep boundary/zero delay, all-sleep idle, wake/exit/cancel, failed-operation atomicity and queue reset/reuse");
}
static void model(void)
{
    struct task_queue q;task_queue_init(&q);unsigned id;for (unsigned i=0;i<TASK_LIMIT;++i) { assert(task_add(&q,&id)==TASK_OK); }
    enum task_status states[TASK_LIMIT]={TASK_READY,TASK_READY,TASK_READY,TASK_READY};uint64_t wake[TASK_LIMIT]={0},dispatches[TASK_LIMIT]={0},sleeps[TASK_LIMIT]={0},wakes[TASK_LIMIT]={0};
    unsigned cursor=TASK_LIMIT-1;uint64_t now=0,total=0;uint32_t random=81726;
    for (unsigned step=0;step<50000;++step) {
        random=random*1664525u+1013904223u;now+=random%7;
        for (unsigned i=0;i<TASK_LIMIT;++i) { if (states[i]==TASK_SLEEPING && now>=wake[i]) { states[i]=TASK_READY;++wakes[i]; } }
        int want=-1;for (unsigned n=1;n<=TASK_LIMIT;++n) { unsigned i=(cursor+n)%TASK_LIMIT;if (states[i]==TASK_READY) { want=(int)i;break; } }
        assert(task_pick(&q,now)==want);
        if (want>=0) {
            cursor=(unsigned)want;++total;++dispatches[want];random=random*1664525u+1013904223u;
            if (random&1) { assert(task_sleep_policy(&q,now,1+random%71)==TASK_OK);states[want]=TASK_SLEEPING;wake[want]=now+1+random%71;++sleeps[want]; }
            else { assert(task_yield_policy(&q)==TASK_OK); }
        }
        assert(q.current==-1 && q.switches==total);
        for (unsigned i=0;i<TASK_LIMIT;++i) { assert(q.tasks[i].status==states[i] && q.tasks[i].deadline==wake[i] && q.tasks[i].dispatches==dispatches[i] && q.tasks[i].sleeps==sleeps[i] && q.tasks[i].wakes==wakes[i]); }
    }
    task_cancel_all(&q);assert(task_queue_done(&q));puts("PASS: 50000 independent scheduling/wakeup decisions, all task states/deadlines/dispatch counts compared with reference, cancellation leaves no runnable work");
}
int main(void) { boundaries();model();return 0; }
