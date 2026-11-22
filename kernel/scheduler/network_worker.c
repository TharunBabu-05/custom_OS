#include "network_worker.h"
#include "task.h"
#include "mmu.h"
#include "irq.h"
#include "cpu.h"
#include "crypto.h"
#include "process.h"

enum worker_state { WORKER_IDLE,WORKER_RUNNING,WORKER_WAITING,WORKER_DONE };
static enum worker_state state;
static struct task_context foreground,worker;
static void (*entry)(void *);
static void *argument;
static uint64_t entry_flags,last_poll;
static bool stopping;

static unsigned char *stack(void)
{ return task_stacks[NETWORK_STACK_SLOT]+TASK_GUARD_BYTES; }

static void check_stack(void)
{
    const unsigned char *p=stack();
    uintptr_t low=(uintptr_t)p;
    bool valid=!(worker.sp&15) && worker.sp>=low+64 && worker.sp<=low+TASK_STACK_BYTES;
    for (unsigned i=0;i<64;++i) { valid=valid && p[i]==0x6d; }
    if (!valid) { irq_fatal_stop();cpu_halt(); }
}
static void resume(void)
{
    uint64_t flags=irq_save();
    check_stack();state=WORKER_RUNNING;
    task_context_switch(&foreground,&worker);
    check_stack();
    if (state==WORKER_DONE) {
        crypto_wipe(stack(),TASK_STACK_BYTES);
        crypto_wipe(&worker,sizeof(worker));
        crypto_wipe(&foreground,sizeof(foreground));
        entry=NULL;argument=NULL;state=WORKER_IDLE;
    }
    irq_restore(flags);
}
static void enter(void)
{
    irq_restore(entry_flags);
    entry(argument);
    (void)irq_save();state=WORKER_DONE;
    task_context_switch(&worker,&foreground);
    cpu_halt();
}
bool network_worker_start(void (*run)(void *),void *context)
{
    if (!run || state!=WORKER_IDLE || !mmu_enabled() || !mmu_dma_compatible() ||
        !irq_timer_running() || !timer_frequency() ||
        mmu_dma_buffer((uintptr_t)task_stacks[NETWORK_STACK_SLOT],TASK_GUARD_BYTES) ||
        !mmu_dma_buffer((uintptr_t)stack(),TASK_STACK_BYTES)) { return false; }
    uint64_t flags=irq_save();
    if (flags&0x80) { irq_restore(flags);return false; }
    entry_flags=flags;irq_restore(flags);
    for (unsigned i=0;i<64;++i) { stack()[i]=0x6d; }
    worker=(struct task_context){.sp=(uintptr_t)stack()+TASK_STACK_BYTES};
    worker.x[11]=(uintptr_t)enter;
    entry=run;argument=context;stopping=false;last_poll=timer_ticks();
    resume();
    return true;
}
bool network_worker_active(void) { return state!=WORKER_IDLE; }
bool network_worker_pause(void)
{
    if (state!=WORKER_RUNNING || stopping) { return false; }
    uint64_t flags=irq_save();
    state=WORKER_WAITING;
    task_context_switch(&worker,&foreground);
    irq_restore(flags);
    return !stopping;
}
bool network_worker_due(void)
{
    return state==WORKER_WAITING && timer_ticks()-last_poll>=timer_frequency()/100;
}
void network_worker_service(void)
{
    if (!network_worker_due()) { return; }
    last_poll=timer_ticks();resume();
}
bool network_worker_stop(void)
{
    if (state==WORKER_IDLE) { return true; }
    if (state!=WORKER_WAITING) { return false; }
    stopping=true;resume();
    return state==WORKER_IDLE;
}

static void test_entry(void *context)
{
    unsigned *stage=context;
    volatile unsigned marker=0x2741;
    *stage=1;
    while (network_worker_pause()) {
        if (marker!=0x2741) { *stage=0;return; }
        ++*stage;
    }
    *stage=marker==0x2741 ? UINT32_MAX : 0;
}
bool network_worker_test(bool with_user)
{
    unsigned stage=0;
    if (!network_worker_start(test_entry,&stage)) { return false; }
    bool suspended=stage==1 && network_worker_active();
    bool user_ok=true;
    if (with_user) {
        struct app_args args;
        user_ok=app_args_parse("/boot/SPIN.ELF",&args) && process_terminal(&args,1)==1;
    } else {
        uint64_t start=timer_ticks();
        while (stage==1 && timer_ticks()-start<timer_frequency()/5) { network_worker_service(); }
    }
    bool resumed=with_user ? stage>=10 : stage==2;
    bool stopped=network_worker_stop();
    return suspended && user_ok && resumed && stopped && stage==UINT32_MAX && !network_worker_active();
}
