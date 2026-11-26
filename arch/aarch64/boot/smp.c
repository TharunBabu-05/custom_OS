#include "arm_platform.h"
#include "app_args.h"
#include "console.h"
#include "cpu.h"
#include "exception.h"
#include "mmu.h"
#include "smp.h"

enum worker_phase { STARTING,READY,PARKED,FAULT_PARKED,STOPPING,FAULTED,BAD_MMU,OFF_RETURNED };
enum { PSCI_ON=0, PSCI_OFF=1, PSCI_PENDING=2, MAX_ROUNDS=1024 };
#define STOP_REQUEST UINT64_MAX
#define FAULT_REQUEST (UINT64_MAX-1)
#define PARK_REQUEST (UINT64_MAX-2)
#define RESUME_REQUEST (UINT64_MAX-3)
#define CANCEL_BIT (1ULL<<63)
#define MAX_SEQUENCE ((CANCEL_BIT>>1)-1)
#define STACK_CANARY 0x6f75726f73736d70ULL
#define CPU_LIFECYCLE_CODE __attribute__((section(".text.cpu_lifecycle")))

struct smp_worker {
    uint64_t stack_top,affinity;    /* Consumed by the physical entry stub. */
    uint64_t phase,entry_el;
    uint64_t request,reply,seed,result,job_ticks;
    uint64_t esr,pc;
    uint64_t launches,verified,resumes;
    int32_t off_status;
    unsigned rounds;
    bool retained;
} __attribute__((aligned(SMP_WORKER_SIZE)));
_Static_assert(sizeof(struct smp_worker)==SMP_WORKER_SIZE &&
               offsetof(struct smp_worker,stack_top)==0 && offsetof(struct smp_worker,affinity)==8,
               "secondary entry layout");

struct smp_worker smp_workers[SMP_WORKER_LIMIT];
static unsigned worker_count;
static enum psci_conduit conduit;
static uint64_t sequence;
static unsigned batch_mask;
extern unsigned char smp_secondary_entry[];
extern void smp_test_break(void);

static uint64_t acquire(const uint64_t *value)
{ return __atomic_load_n(value,__ATOMIC_ACQUIRE); }
static void publish(uint64_t *value,uint64_t next)
{ __atomic_store_n(value,next,__ATOMIC_RELEASE); }
static void wake_workers(void)
{ __asm__ volatile("dsb sy; sev" ::: "memory"); }

static uint64_t checksum(uint64_t input)
{
    for (uint64_t i=0;i<512;++i) { input=(input^i)*1099511628211ULL; }
    return input;
}

static _Noreturn CPU_LIFECYCLE_CODE void worker_poweroff(struct smp_worker *w,enum worker_phase phase)
{
    publish(&w->phase,phase);
    __asm__ volatile("dsb sy" ::: "memory");
    w->off_status=psci_call(conduit,PSCI_CPU_OFF,0);
    publish(&w->phase,OFF_RETURNED);
    cpu_halt();
    __builtin_unreachable();
}

CPU_LIFECYCLE_CODE void smp_worker_exception(struct smp_worker *w,struct exception_frame *f)
{
    if (acquire(&w->phase)>=STOPPING) {
        cpu_halt();
        __builtin_unreachable();
    }
    w->esr=f->esr;
    w->pc=f->elr;
    if (f->vector==4 && f->esr==0xf2000041ULL && f->elr==(uintptr_t)smp_test_break &&
        acquire(&w->request)==FAULT_REQUEST) {
        f->elr+=4;
        return;
    }
    worker_poweroff(w,FAULTED);
}

static void execute_checksum(struct smp_worker *w,uint64_t request)
{
    uint64_t start=timer_ticks(),result=w->seed;
    unsigned rounds=w->rounds;
    for (unsigned i=0;i<rounds;++i) {
        result=checksum(result);
        if (acquire(&w->request)!=request) { return; }
    }
    w->result=result;
    w->job_ticks=timer_ticks()-start;
    publish(&w->reply,request);
}

_Noreturn CPU_LIFECYCLE_CODE void smp_worker_main(struct smp_worker *w,uint64_t entry_el)
{
    w->entry_el=entry_el;
    if (!mmu_enable_secondary()) { worker_poweroff(w,BAD_MMU); }
    publish(&w->phase,READY);
    uint64_t seen=0;
    for (;;) {
        uint64_t request=acquire(&w->request);
        if (request==seen) { __asm__ volatile("wfe" ::: "memory");continue; }
        seen=request;
        if (request==STOP_REQUEST) { worker_poweroff(w,STOPPING); }
        if (request==FAULT_REQUEST) {
            smp_test_break();
            publish(&w->phase,FAULT_PARKED);
        } else if (request==PARK_REQUEST) { publish(&w->phase,PARKED); }
        else if (request==RESUME_REQUEST) { publish(&w->phase,READY); }
        else if (request&CANCEL_BIT) { publish(&w->reply,request); }
        else { execute_checksum(w,request); }
    }
}

static void cpu_label(const struct smp_worker *w)
{ console_write("Secondary CPU ");console_hex(w->affinity);console_write(" "); }

static void firmware_status(int32_t status)
{
    if (status<0) { console_write("-"); }
    console_u64(status<0 ? (uint64_t)(-(int64_t)status) : (uint64_t)status);
}

static int32_t affinity_state(const struct smp_worker *w)
{ return psci_call(conduit,PSCI_AFFINITY_INFO,w->affinity); }

static CPU_LIFECYCLE_CODE bool discover(const void *dtb)
{
    if (conduit!=PSCI_NONE) { return true; }
    struct arm_topology topology;
    enum psci_conduit method=psci_discover(dtb,0x200000);
    if (method==PSCI_NONE || !arm_topology_read(dtb,0x200000,&topology)) {
        console_write("Secondary CPU topology or PSCI unavailable.\n");return false;
    }
    uint64_t boot=arm_cpu_affinity();
    unsigned count=0;
    for (unsigned i=0;i<topology.count;++i) {
        const struct arm_cpu *cpu=&topology.cpu[i];
        if (!cpu->enabled || !cpu->psci || cpu->affinity==boot) { continue; }
        if (count==SMP_WORKER_LIMIT) {
            console_write("Secondary CPU topology exceeds worker capacity.\n");return false;
        }
        smp_workers[count++].affinity=cpu->affinity;
    }
    if (!count) { console_write("No PSCI secondary CPUs in the device tree.\n");return false; }
    int32_t version=psci_call(method,PSCI_VERSION,0);
    if (version<2 || psci_call(method,PSCI_AFFINITY_INFO,boot)!=PSCI_ON ||
        (version>=0x10000 && (psci_call(method,PSCI_FEATURES,PSCI_CPU_ON)<0 ||
                             psci_call(method,PSCI_FEATURES,PSCI_CPU_OFF)<0))) {
        console_write("Firmware refused secondary CPU lifecycle support.\n");return false;
    }
    worker_count=count;
    conduit=method;
    return true;
}

static bool stack_intact(const struct smp_worker *w)
{ return *(const volatile uint64_t *)(uintptr_t)(w->stack_top-PAGE_SIZE)==STACK_CANARY; }

static bool release_stack(struct smp_worker *w)
{
    bool intact=stack_intact(w);
    if (!page_free(w->stack_top-PAGE_SIZE)) {
        w->retained=true;cpu_label(w);console_write("stack release failed.\n");return false;
    }
    w->stack_top=0;
    w->retained=false;
    if (!intact) { cpu_label(w);console_write("stack canary changed.\n"); }
    return intact;
}

static bool online(const struct smp_worker *w)
{ return w->stack_top && !w->retained && acquire(&w->phase)==READY; }

static bool parked(uint64_t phase)
{ return phase==PARKED || phase==FAULT_PARKED; }

static bool await_phase(const struct smp_worker *w,uint64_t phase,uint64_t next)
{
    uint64_t start=timer_ticks(),hz=timer_frequency();
    do {
        uint64_t observed=acquire(&w->phase);
        if (observed==next) { return stack_intact(w); }
        if (observed!=phase) { break; }
        __asm__ volatile("yield");
    } while (timer_ticks()-start<hz);
    return false;
}

static bool transition(struct smp_worker *w,uint64_t request,uint64_t phase,uint64_t next)
{
    publish(&w->request,request);
    wake_workers();
    if (await_phase(w,phase,next)) { return true; }
    w->retained=true;
    cpu_label(w);console_write("transition unverified, stack retained.\n");
    return false;
}

static bool start_worker(struct smp_worker *w)
{
    if (online(w)) { return true; }
    if (w->stack_top && !w->retained) {
        uint64_t phase=acquire(&w->phase);
        if (parked(phase) && stack_intact(w) && affinity_state(w)==PSCI_ON) {
            if (!transition(w,RESUME_REQUEST,phase,READY)) { return false; }
            ++w->resumes;
            cpu_label(w);console_write("resumed at EL1.\n");
            return true;
        }
    }
    if (w->stack_top || affinity_state(w)!=PSCI_OFF) {
        cpu_label(w);console_write("startup unavailable. See smpinfo.\n");return false;
    }
#ifdef OUROS_PI5
    /* Some Pi firmware leaves a core pending on CPU_ON after CPU_OFF. */
    if (acquire(&w->phase)>=STOPPING) {
        cpu_label(w);console_write("reboot required after CPU_OFF.\n");return false;
    }
#endif
    uint64_t page;
    if (!page_alloc(&page)) { console_write("No page for secondary CPU stack.\n");return false; }
    if (!mmu_dma_buffer(page,PAGE_SIZE)) {
        (void)page_free(page);console_write("Secondary CPU stack requires writable Normal-NC RAM.\n");return false;
    }
    *(volatile uint64_t *)(uintptr_t)page=STACK_CANARY;
    w->entry_el=w->esr=w->pc=w->job_ticks=0;
    w->off_status=0;
    publish(&w->phase,STARTING);
    publish(&w->reply,0);
    publish(&w->request,0);
    publish(&w->stack_top,page+PAGE_SIZE);
    __asm__ volatile("dsb sy" ::: "memory");
    int32_t status=psci_call_args(conduit,PSCI_CPU_ON,w->affinity,(uintptr_t)smp_secondary_entry,(uintptr_t)w);
    if (status) {
        cpu_label(w);console_write("PSCI CPU_ON returned ");firmware_status(status);console_write(".\n");
        if (affinity_state(w)==PSCI_OFF) { (void)release_stack(w); }
        else { w->retained=true; }
        return false;
    }
    if (await_phase(w,STARTING,READY)) {
        ++w->launches;
        cpu_label(w);console_write("online at EL1, entered EL");console_u64(w->entry_el);console_write(".\n");
        return true;
    }
    w->retained=true;
    publish(&w->request,STOP_REQUEST);wake_workers();
    cpu_label(w);console_write("startup unverified, stack retained.\n");
    return false;
}

static CPU_LIFECYCLE_CODE bool reap_offline(struct smp_worker *w)
{
    uint64_t start=timer_ticks(),hz=timer_frequency();
    uint64_t next_poll=0;
    do {
        uint64_t phase=acquire(&w->phase);
        if (phase==OFF_RETURNED) { break; }
        uint64_t elapsed=timer_ticks()-start;
        if (phase>=STOPPING && elapsed>=next_poll) {
            if (affinity_state(w)==PSCI_OFF) { return release_stack(w); }
            next_poll=elapsed+hz/1000+1;
        }
        __asm__ volatile("yield");
    } while (timer_ticks()-start<hz);
    w->retained=true;
    cpu_label(w);console_write("shutdown unverified, stack retained.\n");
    return false;
}

bool smp_shutdown(void)
{
    bool active=false,clean=true;
    for (unsigned i=0;i<worker_count;++i) {
        struct smp_worker *w=&smp_workers[i];
        if (w->stack_top) { publish(&w->request,STOP_REQUEST);active=true; }
    }
    batch_mask=0;
    if (!active) { return true; }
    wake_workers();
    for (unsigned i=0;i<worker_count;++i) {
        struct smp_worker *w=&smp_workers[i];
        if (w->stack_top && !reap_offline(w)) { clean=false; }
    }
    return clean;
}

static void park_workers(unsigned mask)
{
    unsigned count=0;
    for (unsigned i=0;i<worker_count;++i) {
        if (!(mask&(1u<<i))) { continue; }
        struct smp_worker *w=&smp_workers[i];
        if (!w->stack_top || w->retained) { continue; }
        uint64_t phase=acquire(&w->phase);
        if (parked(phase) || (phase==READY && transition(w,PARK_REQUEST,READY,PARKED))) { ++count; }
    }
    console_write("Secondary CPUs parked=");console_u64(count);console_write("\n");
}

static void information(void)
{
    for (unsigned i=0;i<worker_count;++i) {
        const struct smp_worker *w=&smp_workers[i];
        uint64_t phase=acquire(&w->phase);
        const char *state=!w->stack_top ? "off" : w->retained ? "retained" :
                          phase==READY ? "online" : phase==PARKED ? "parked" :
                          phase==FAULT_PARKED ? "fault-parked" : "transition";
        int32_t psci=affinity_state(w);
        cpu_label(w);console_write("state=");console_write(state);
        console_write(" launches=");console_u64(w->launches);
        console_write(" jobs=");console_u64(w->verified);
        if (w->stack_top) { console_write(" stack=");console_hex(w->stack_top-PAGE_SIZE); }
        console_write(" resumes=");console_u64(w->resumes);
        console_write(" psci=");console_write(psci==PSCI_ON ? "on" : psci==PSCI_OFF ? "off" :
                                               psci==PSCI_PENDING ? "pending" : "unavailable");
        if (phase!=STARTING && phase!=READY) {
            if (phase==BAD_MMU) { console_write(" MMU setup failed"); }
            if (phase==OFF_RETURNED) { console_write(" CPU_OFF status=");firmware_status(w->off_status); }
            if ((phase==FAULT_PARKED || phase==FAULTED || phase==OFF_RETURNED) && w->esr) {
                console_write(" ESR=");console_hex(w->esr);
                console_write(" PC=");console_hex(w->pc);
            }
        }
        console_write("\n");
    }
    if (batch_mask) {
        unsigned ready=0,count=0;
        for (unsigned i=0;i<worker_count;++i) {
            if (!(batch_mask&(1u<<i))) { continue; }
            ++count;
            ready+=acquire(&smp_workers[i].reply)==sequence;
        }
        console_write("Secondary batch=");console_u64(sequence);
        console_write(" replies=");console_u64(ready);console_write("/");console_u64(count);console_write("\n");
    }
}

static bool submit_batch(unsigned rounds)
{
    if (sequence==MAX_SEQUENCE) { console_write("Secondary CPU sequence exhausted.\n");return false; }
    unsigned mask=0;
    for (unsigned i=0;i<worker_count;++i) {
        if (online(&smp_workers[i])) { mask|=1u<<i; }
    }
    if (!mask) { console_write("Start secondary CPUs with smpstart.\n");return false; }
    uint64_t request=++sequence;
    batch_mask=mask;
    for (unsigned i=0;i<worker_count;++i) {
        if (!(mask&(1u<<i))) { continue; }
        struct smp_worker *w=&smp_workers[i];
        w->seed=(request<<16)^w->affinity^STACK_CANARY;
        w->rounds=rounds;
        publish(&w->request,request);
    }
    wake_workers();
    return true;
}

static bool await_reply(struct smp_worker *w,uint64_t request,uint64_t start,uint64_t hz)
{
    do {
        if (acquire(&w->reply)==request) { return stack_intact(w); }
        if (acquire(&w->phase)!=READY) { break; }
        __asm__ volatile("yield");
    } while (timer_ticks()-start<hz);
    return false;
}

static void collect_batch(bool cancel)
{
    if (!batch_mask) { console_write("No secondary batch.\n");return; }
    uint64_t request=sequence;
    if (cancel) {
        /* A unique acknowledgement prevents an earlier cancel satisfying this one. */
        request|=CANCEL_BIT;
        for (unsigned i=0;i<worker_count;++i) {
            if (batch_mask&(1u<<i)) { publish(&smp_workers[i].request,request); }
        }
        wake_workers();
    }
    uint64_t start=timer_ticks(),hz=timer_frequency();
    unsigned count=0,verified=0;
    for (unsigned i=0;i<worker_count;++i) {
        if (!(batch_mask&(1u<<i))) { continue; }
        ++count;
        struct smp_worker *w=&smp_workers[i];
        bool good=await_reply(w,request,start,hz);
        if (good && !cancel) {
            uint64_t expected=w->seed;
            for (unsigned round=0;round<w->rounds;++round) { expected=checksum(expected); }
            good=w->result==expected;
        }
        if (!good) {
            cpu_label(w);console_write("work or stack verification failed.\n");
            w->retained=true;
            continue;
        }
        ++verified;
        if (cancel) { continue; }
        ++w->verified;
        cpu_label(w);console_write("job=");console_u64(request);
        console_write(" ticks=");console_u64(w->job_ticks);console_write("\n");
    }
    batch_mask=0;
    bool pass=verified==count;
    if (cancel) { console_write(pass ? "Secondary batch cancelled.\n" : "FAIL: secondary batch cancellation.\n"); }
    else {
        console_write(pass ? "PASS: secondary checksum jobs=" : "FAIL: secondary checksum jobs=");
        console_u64(count);console_write("\n");
    }
}

static void test_fault(unsigned mask)
{
    for (unsigned i=0;i<worker_count;++i) {
        struct smp_worker *w=&smp_workers[i];
        if (!(mask&(1u<<i)) || !online(w)) { continue; }
        bool pass=transition(w,FAULT_REQUEST,READY,FAULT_PARKED) &&
                  w->esr==0xf2000041ULL && w->pc==(uintptr_t)smp_test_break;
        console_write(pass ? "PASS: secondary fault captured, worker parked.\n" :
                             "FAIL: secondary fault recovery.\n");
        information();
        return;
    }
    console_write("Start secondary CPUs with smpstart.\n");
}

static unsigned select_workers(const char *arg)
{
    if (!*arg) { return (1u<<worker_count)-1; }
    if (arg[0]!='0' || arg[1]!='x' || !arg[2]) { return 0; }
    uint64_t affinity=0;
    for (arg+=2;*arg;++arg) {
        unsigned digit=(unsigned)(*arg-'0');
        if (digit>9) { digit=(unsigned)((*arg|32)-'a')+10; }
        if (digit>15 || (affinity>>60)) { return 0; }
        affinity=(affinity<<4)|digit;
    }
    for (unsigned i=0;i<worker_count;++i) {
        if (smp_workers[i].affinity==affinity) { return 1u<<i; }
    }
    return 0;
}

bool smp_command(const char *line,const void *dtb)
{
    enum { START,INFO,TEST,FAULT,STOP,PARK,SUBMIT,WAIT,CANCEL,COMMAND_COUNT };
    static const char *const commands[COMMAND_COUNT]={
        "smpstart","smpinfo","smptest","smpfault","smpstop","smppark","smpsubmit","smpwait","smpcancel"
    };
    unsigned cmd=0;
    const char *arg=line;
    for (;cmd<COMMAND_COUNT;++cmd) {
        const char *name=commands[cmd];
        arg=line;
        while (*name && *name==*arg) { ++name;++arg; }
        if (!*name && (!*arg || *arg==' ')) { break; }
    }
    if (cmd==COMMAND_COUNT) { return false; }
    while (*arg==' ') { ++arg; }
    bool targeted=cmd==START || cmd==PARK || cmd==FAULT;
    unsigned rounds=1;
    if (*arg && !targeted && (cmd!=SUBMIT || !app_args_uint(arg,MAX_ROUNDS,&rounds) || !rounds)) {
        console_write("Invalid SMP arguments. smpsubmit accepts 1..1024 rounds.\n");return true;
    }
    if (cmd==STOP) {
        console_write(smp_shutdown() ? "Secondary CPU stacks released.\n" : "Some secondary CPU stacks are retained.\n");
        return true;
    }
    if (cmd==WAIT || cmd==CANCEL) { collect_batch(cmd==CANCEL);return true; }
    if (batch_mask && cmd!=INFO) {
        console_write("Secondary batch pending. Use smpwait or smpcancel.\n");return true;
    }
    if (cmd==START && !(mmu_enabled() && mmu_dma_compatible())) {
        console_write("Run mmuinit before smpstart.\n");return true;
    }
    if (!discover(dtb)) { return true; }
    unsigned mask=targeted ? select_workers(arg) : 0;
    if (targeted && !mask) {
        console_write("Unknown secondary CPU affinity. Use smpinfo and a 0x value.\n");return true;
    }
    if (cmd==START) {
        unsigned count=0;
        for (unsigned i=0;i<worker_count;++i) {
            struct smp_worker *w=&smp_workers[i];
            if (mask&(1u<<i)) { (void)start_worker(w); }
            count+=online(w);
        }
        console_write("Secondary CPUs online=");console_u64(count);console_write("\n");
    } else if (cmd==INFO) { information(); }
    else if (cmd==PARK) { park_workers(mask); }
    else if (cmd==FAULT) { test_fault(mask); }
    else if (submit_batch(rounds)) {
        if (cmd==TEST) { collect_batch(false); }
        else { console_write("Secondary batch submitted. Use smpwait or smpcancel.\n"); }
    }
    return true;
}
