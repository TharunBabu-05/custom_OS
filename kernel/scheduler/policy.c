#include "task.h"
void task_queue_init(struct task_queue *q)
{ if (q) { *q=(struct task_queue){.current=-1,.cursor=TASK_LIMIT-1}; } }
static bool valid(const struct task_queue *q)
{ return q && q->current>=-1 && q->current<TASK_LIMIT && q->cursor<TASK_LIMIT; }
enum task_result task_add(struct task_queue *q,unsigned *id)
{
    if (!valid(q) || !id) { return TASK_ARGUMENT; }
    for (unsigned i=0;i<TASK_LIMIT;++i) { if (q->tasks[i].status==TASK_UNUSED) { q->tasks[i]=(struct task_record){.status=TASK_READY};*id=i;return TASK_OK; } }return TASK_FULL;
}
int task_pick(struct task_queue *q,uint64_t now)
{
    if (!valid(q) || q->current!=-1 || q->switches==UINT64_MAX) { return -1; }
    for (unsigned i=0;i<TASK_LIMIT;++i) { struct task_record *t=&q->tasks[i];if (t->status==TASK_SLEEPING && now>=t->deadline) { t->status=TASK_READY;++t->wakes; } }
    for (unsigned n=1;n<=TASK_LIMIT;++n) {
        unsigned id=(q->cursor+n)%TASK_LIMIT;struct task_record *t=&q->tasks[id];
        if (t->status==TASK_READY && t->dispatches<UINT64_MAX) { t->status=TASK_RUNNING;++t->dispatches;q->current=(int)id;q->cursor=id;++q->switches;return (int)id; }
    }return -1;
}
static struct task_record *current(struct task_queue *q)
{ return valid(q) && q->current>=0 && q->tasks[q->current].status==TASK_RUNNING ? &q->tasks[q->current] : NULL; }
enum task_result task_yield_policy(struct task_queue *q)
{ struct task_record *t=current(q);if (!t) { return TASK_STATE; }if (t->yields==UINT64_MAX) { return TASK_OVERFLOW; }++t->yields;t->status=TASK_READY;q->current=-1;return TASK_OK; }
enum task_result task_sleep_policy(struct task_queue *q,uint64_t now,uint64_t delay)
{
    struct task_record *t=current(q);if (!t) { return TASK_STATE; }if (!delay) { return task_yield_policy(q); }
    if (delay>UINT64_MAX-now || t->sleeps==UINT64_MAX || t->wakes==UINT64_MAX) { return TASK_OVERFLOW; }
    t->deadline=now+delay;++t->sleeps;t->status=TASK_SLEEPING;q->current=-1;return TASK_OK;
}
enum task_result task_exit_policy(struct task_queue *q,int code)
{ struct task_record *t=current(q);if (!t) { return TASK_STATE; }t->exit_code=code;t->status=TASK_EXITED;q->current=-1;return TASK_OK; }
bool task_queue_done(const struct task_queue *q)
{
    if (!valid(q) || q->current!=-1) { return false; }
    for (unsigned i=0;i<TASK_LIMIT;++i) { enum task_status s=q->tasks[i].status;if (s==TASK_READY || s==TASK_RUNNING || s==TASK_SLEEPING) { return false; } }return true;
}
void task_cancel_all(struct task_queue *q)
{
    if (!valid(q) || q->current!=-1) { return; }
    for (unsigned i=0;i<TASK_LIMIT;++i) { struct task_record *t=&q->tasks[i];if (t->status==TASK_READY || t->status==TASK_SLEEPING) { t->status=TASK_CANCELLED;t->exit_code=-1; } }
}
const char *task_status_text(enum task_status s)
{ switch (s) { case TASK_UNUSED:return "unused";case TASK_READY:return "ready";case TASK_RUNNING:return "running";case TASK_SLEEPING:return "sleeping";case TASK_EXITED:return "exited";case TASK_CANCELLED:return "cancelled"; }return "invalid"; }
