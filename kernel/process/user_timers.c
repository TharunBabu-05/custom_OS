#include "user_timers.h"

enum { TIMER_SLOT_BITS=4 };
_Static_assert(UOS_TIMER_MAX==(1u<<TIMER_SLOT_BITS),"timer handle slot layout");
struct timer_slot {
    uint64_t token,owner;
    struct uos_timer_info info;
};
static struct timer_slot timers[UOS_TIMER_MAX];
static uint64_t sequence;

static struct timer_slot *find(uint64_t owner,uint64_t token)
{
    struct timer_slot *slot=&timers[token&(UOS_TIMER_MAX-1)];
    return owner && token && slot->token==token && slot->owner==owner ? slot : NULL;
}

int64_t user_timer_open(uint64_t owner)
{
    if (!owner) { return UOS_ERROR; }
    unsigned free_slot=UOS_TIMER_MAX,count=0;
    for (unsigned i=0;i<UOS_TIMER_MAX;++i) {
        if (timers[i].owner==owner) { ++count; }
        if (!timers[i].owner && free_slot==UOS_TIMER_MAX) { free_slot=i; }
    }
    if (count>=UOS_TIMER_OWNER_MAX || free_slot==UOS_TIMER_MAX || sequence==((uint64_t)INT64_MAX>>TIMER_SLOT_BITS)) {
        return UOS_ENOSPC;
    }
    uint64_t token=(++sequence<<TIMER_SLOT_BITS)|free_slot;
    timers[free_slot]=(struct timer_slot){.token=token,.owner=owner};
    return (int64_t)token;
}

int64_t user_timer_arm(uint64_t owner,uint64_t token,uint64_t now,uint64_t value,uint64_t interval,bool absolute)
{
    struct timer_slot *slot=find(owner,token);
    if (!slot) { return UOS_EBADF; }
    if (!value || interval>UOS_TIMER_MAX_MS) { return UOS_ERROR; }
    uint64_t deadline=value;
    if (absolute) {
        if (value>now && value-now>UOS_TIMER_MAX_MS) { return UOS_ERROR; }
    } else {
        if (value>UOS_TIMER_MAX_MS || value>UINT64_MAX-now) { return UOS_ERROR; }
        deadline=now+value;
    }
    slot->info=(struct uos_timer_info){.deadline_ms=deadline,.interval_ms=interval};
    return 0;
}

int64_t user_timer_disarm(uint64_t owner,uint64_t token)
{
    struct timer_slot *slot=find(owner,token);
    if (!slot) { return UOS_EBADF; }
    slot->info=(struct uos_timer_info){0};
    return 0;
}

static void update(struct uos_timer_info *timer,uint64_t now)
{
    if (!timer->deadline_ms || now<timer->deadline_ms) { return; }
    uint64_t elapsed=now-timer->deadline_ms,period=timer->interval_ms;
    uint64_t count=period ? 1+elapsed/period : 1;
    if (count>UINT64_MAX-timer->pending) { timer->pending=UINT64_MAX; }
    else { timer->pending+=count; }
    uint64_t advance=period ? period-elapsed%period : 0;
    timer->deadline_ms=advance && advance<=UINT64_MAX-now ? now+advance : 0;
}

int64_t user_timer_info(uint64_t owner,uint64_t token,uint64_t now,struct uos_timer_info *out)
{
    struct timer_slot *slot=find(owner,token);
    if (!slot) { return UOS_EBADF; }
    update(&slot->info,now);
    *out=slot->info;
    return 0;
}

int64_t user_timer_read(uint64_t owner,uint64_t token,uint64_t now,uint64_t *out)
{
    struct timer_slot *slot=find(owner,token);
    if (!slot) { return UOS_EBADF; }
    update(&slot->info,now);
    if (!slot->info.pending) { return UOS_EAGAIN; }
    *out=slot->info.pending;
    slot->info.pending=0;
    return 0;
}

int64_t user_timer_close(uint64_t owner,uint64_t token)
{
    struct timer_slot *slot=find(owner,token);
    if (!slot) { return UOS_EBADF; }
    *slot=(struct timer_slot){0};
    return 0;
}

void user_timers_close_owner(uint64_t owner)
{
    if (!owner) { return; }
    for (unsigned i=0;i<UOS_TIMER_MAX;++i) {
        if (timers[i].owner==owner) { timers[i]=(struct timer_slot){0}; }
    }
}

unsigned user_timers_count(void)
{
    unsigned count=0;
    for (unsigned i=0;i<UOS_TIMER_MAX;++i) { count+=timers[i].owner!=0; }
    return count;
}
