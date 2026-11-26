#include "cpu.h"
#include "smp_lock.h"

#define CHOOSING UINT32_MAX

static uint32_t ticket(const struct smp_lock *lock,unsigned slot)
{ return __atomic_load_n(&lock->tickets[slot],__ATOMIC_SEQ_CST); }

static void set_ticket(struct smp_lock *lock,unsigned slot,uint32_t value)
{ __atomic_store_n(&lock->tickets[slot],value,__ATOMIC_SEQ_CST); }

bool smp_lock_acquire(struct smp_lock *lock,unsigned slot,uint64_t start,uint64_t budget)
{
    if (slot>=SMP_LOCK_SLOTS || ticket(lock,slot) || timer_ticks()-start>=budget) { return false; }

    /* The doorway and ticket share one atomic word. The boot CPU and
     * each worker write only their own word. */
    set_ticket(lock,slot,CHOOSING);
    uint32_t highest=0;
    for (unsigned peer=0;peer<SMP_LOCK_SLOTS;++peer) {
        uint32_t other=ticket(lock,peer);
        if (other!=CHOOSING && other>highest) { highest=other; }
    }
    if (highest==CHOOSING-1) { goto withdraw; }
    uint32_t mine=highest+1;
    set_ticket(lock,slot,mine);

    for (unsigned peer=0;peer<SMP_LOCK_SLOTS;++peer) {
        if (peer==slot) { continue; }
        for (;;) {
            uint32_t other=ticket(lock,peer);
            if (other!=CHOOSING && (!other || other>mine || (other==mine && peer>slot))) { break; }
            if (timer_ticks()-start>=budget) { goto withdraw; }
            __asm__ volatile("yield");
        }
    }
    if (timer_ticks()-start<budget) { return true; }

withdraw:
    set_ticket(lock,slot,0);
    return false;
}

void smp_lock_release(struct smp_lock *lock,unsigned slot)
{ set_ticket(lock,slot,0); }

bool smp_lock_idle(const struct smp_lock *lock)
{
    for (unsigned slot=0;slot<SMP_LOCK_SLOTS;++slot) {
        if (ticket(lock,slot)) { return false; }
    }
    return true;
}
