#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "tick.h"
static void preserved(struct tick_state *s,uint64_t now,enum tick_result expected)
{ struct tick_state before=*s;assert(tick_advance(s,now)==expected && !memcmp(s,&before,sizeof(before))); }
static void exact(void)
{
    struct tick_state s;
    assert(tick_init(54000000,100,1234,&s));
    assert(s.period==540000 && s.deadline==541234 && !s.delivered && !s.periods && !s.late);
    preserved(&s,s.deadline-1,TICK_EARLY);
    for (unsigned i=0;i<10000;++i) {
        uint64_t previous=s.deadline;
        assert(tick_advance(&s,previous)==TICK_OK && s.deadline==previous+540000 && s.delivered==i+1 && s.periods==i+1 && !s.late);
    }
    assert(tick_init(62500000,100,0,&s) && s.period==625000);
    assert(tick_advance(&s,625000)==TICK_OK && s.deadline==1250000);
    assert(tick_init(1001,100,0,&s) && s.period==10); /* Rounded period, not an invented exact 100 Hz rate. */
    assert(tick_advance(&s,10)==TICK_OK && s.deadline==20);
    puts("PASS: 54/62.5 MHz timer periods, 10000 exact expirations, early IRQ refusal without mutation, and non-divisible requested-rate behavior");
}
static void delays(void)
{
    struct tick_state s;assert(tick_init(1000,100,1,&s));
    assert(tick_advance(&s,11)==TICK_OK && s.deadline==21 && s.delivered==1 && s.periods==1 && !s.late);
    assert(tick_advance(&s,56)==TICK_OK && s.deadline==61 && s.delivered==2 && s.periods==5 && s.late==3);
    preserved(&s,60,TICK_EARLY);
    assert(tick_advance(&s,1000000000000ULL)==TICK_OK && s.deadline==1000000000001ULL && s.delivered==3 && s.periods==99999999999ULL && s.late==99999999996ULL);
    assert(tick_advance(&s,s.deadline)==TICK_OK && s.delivered==4 && s.periods==100000000000ULL && s.late==99999999996ULL);
    /* A slow, independent single-period reference for randomized delays. */
    assert(tick_init(12345,100,17,&s));uint64_t reference_deadline=140,periods=0,delivered=0,late=0;uint32_t random=7;
    for (unsigned i=0;i<30000;++i) {
        random=random*1664525u+1013904223u;uint64_t now=reference_deadline-1+(random%12000);
        if (now<reference_deadline) { preserved(&s,now,TICK_EARLY);continue; }
        uint64_t elapsed=0;do { ++elapsed;reference_deadline+=123; }while (reference_deadline<=now);
        ++delivered;periods+=elapsed;late+=elapsed-1;
        assert(tick_advance(&s,now)==TICK_OK && s.deadline==reference_deadline && s.deadline>now && s.delivered==delivered && s.periods==periods && s.late==late);
    }
    puts("PASS: long stalls skip to a future absolute deadline in one operation, missed-period accounting, and 30000 independent delayed-delivery comparisons");
}
static void invalid(void)
{
    struct tick_state sentinel,s;memset(&sentinel,0xa5,sizeof(sentinel));
    const uint64_t frequencies[]={0,1,99,UINT64_MAX};const unsigned rates[]={100,0,100,1};
    for (unsigned i=0;i<4;++i) { s=sentinel;assert(!tick_init(frequencies[i],rates[i],0,&s) && !memcmp(&s,&sentinel,sizeof(s))); }
    s=sentinel;assert(!tick_init(1000,1001,0,&s) && !memcmp(&s,&sentinel,sizeof(s)));
    assert(!tick_init(1000,100,UINT64_MAX-9,&s) && !memcmp(&s,&sentinel,sizeof(s)) && !tick_init(1000,100,0,NULL));
    assert(tick_advance(NULL,0)==TICK_INVALID);
    assert(tick_init(1000,100,0,&s));struct tick_state valid=s;
    s.requested_hz=0;preserved(&s,10,TICK_INVALID);s=valid;s.period=0;preserved(&s,10,TICK_INVALID);
    s=valid;s.period++;preserved(&s,10,TICK_INVALID);s=valid;s.deadline=0;preserved(&s,10,TICK_INVALID);
    s=valid;s.delivered=1;preserved(&s,10,TICK_INVALID);s=valid;s.late=1;preserved(&s,10,TICK_INVALID);
    assert(tick_init(1,1,UINT64_MAX-1,&s) && s.deadline==UINT64_MAX);preserved(&s,UINT64_MAX,TICK_OVERFLOW);
    assert(tick_init(1000,100,UINT64_MAX-15,&s));preserved(&s,UINT64_MAX-4,TICK_OVERFLOW);
    assert(tick_init(1,1,0,&s));s.delivered=s.periods=UINT64_MAX;preserved(&s,1,TICK_OVERFLOW);
    assert(tick_init(1,1,0,&s));s.periods=UINT64_MAX;s.late=UINT64_MAX;preserved(&s,2,TICK_OVERFLOW);
    assert(tick_init((uint64_t)INT64_MAX,1,0,&s));assert(tick_advance(&s,s.deadline)==TICK_OK && s.deadline==UINT64_MAX-1);preserved(&s,s.deadline,TICK_OVERFLOW);
    puts("PASS: initialization/corrupt-state validation, overflow in deadlines/counters and unsupported counter wrap preserve state without scheduling into the past");
}
int main(void) { exact();delays();invalid();return 0; }
