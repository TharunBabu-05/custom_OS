/* First cooperative kernel tasks. Context switches happen only at explicit
 * yield/sleep/exit calls. No kernel driver is preempted by this scheduler. */
#include "task.h"
#include "irq.h"
#include "cpu.h"
#include "mmu.h"
#include "heap.h"
#include "memory.h"
#include "console.h"
#include "kfile.h"
#include "vfs.h"
_Alignas(4096) unsigned char task_stacks[TASK_LIMIT][TASK_STACK_STRIDE];
static struct task_queue queue={.current=-1};
static struct task_context contexts[TASK_LIMIT],monitor;
static bool attempted,active,failed;
static uint64_t initial_irq,started,elapsed;
static unsigned completed[TASK_LIMIT],checks[TASK_LIMIT],trace[128],trace_count,highwater[TASK_LIMIT];
static uint64_t run_count;
static uint64_t private_irqs;
static bool io_mode;
static uint64_t io_handles[TASK_LIMIT],io_shared[TASK_LIMIT];
static unsigned auto_closed;
static void *owned[TASK_LIMIT];
static const char *const io_paths[]={"/tmp/.task-io/a","/tmp/.task-io/b","/tmp/.task-io/c"};
static uint64_t owner_for(unsigned id) { return run_count*8+id+2; }
uint64_t task_file_owner(void)
{ return active && queue.current>=0 && queue.current<TASK_LIMIT ? owner_for((unsigned)queue.current) : 1; }
extern uint64_t irq_test_registers(uint64_t duration_ticks);
static uintptr_t bottom(unsigned id) { return (uintptr_t)task_stacks[id]+TASK_GUARD_BYTES; }
static uintptr_t top(unsigned id) { return (uintptr_t)task_stacks[id]+TASK_STACK_STRIDE; }
static void clean(void *p,size_t n) { volatile unsigned char *d=p;while (n--) { *d++=0; } }
static void broken(void) __attribute__((noreturn));
static void broken(void)
{ irq_fatal_stop();console_write("FATAL: task context/stack ownership failed. CPU halted.\n");cpu_halt();for (;;) {} }
static bool stack_ok(unsigned id,bool saved)
{
    const unsigned char *p=(const unsigned char *)bottom(id);
    for (unsigned i=0;i<64;++i) { if (p[i]!=0x6d) { return false; } }
    uintptr_t sp=contexts[id].sp;return !saved || (!(sp&15) && sp>=bottom(id)+64 && sp<=top(id));
}
static bool suspend(enum task_result r,int id,uint64_t flags)
{
    if (r!=TASK_OK) { irq_restore(flags);return false; }
    if (!stack_ok((unsigned)id,false)) { broken(); }
    task_context_switch(&contexts[id],&monitor);
    /* Resumed on the same task's stack; flags is part of that stack frame. */
    if (queue.current!=id || queue.tasks[id].status!=TASK_RUNNING) { broken(); }
    irq_restore(flags);return true;
}
bool task_yield(void)
{
    uint64_t flags=irq_save();int id=queue.current;
    if (!active || id<0 || id>=TASK_LIMIT) { irq_restore(flags);return false; }
    return suspend(task_yield_policy(&queue),id,flags);
}
bool task_sleep_ms(unsigned ms)
{
    if (!ms) { return task_yield(); }uint64_t flags=irq_save();int id=queue.current;
    uint64_t frequency=timer_frequency();
    if (!active || id<0 || id>=TASK_LIMIT || ms>60000 || frequency>(UINT64_MAX-999)/ms) { irq_restore(flags);return false; }
    uint64_t delay=(frequency*ms+999)/1000;if (!delay) { irq_restore(flags);return false; }
    return suspend(task_sleep_policy(&queue,timer_ticks(),delay),id,flags);
}
void task_exit(int code)
{
    (void)irq_save();int id=queue.current;
    if (!active || id<0 || id>=TASK_LIMIT || !stack_ok((unsigned)id,false)) { broken(); }
    auto_closed+=kfile_close_owner(owner_for((unsigned)id));
    if (task_exit_policy(&queue,code)!=TASK_OK) { broken(); }
    task_context_switch(&contexts[id],&monitor);broken();
}
static void worker(unsigned id)
{
    /* Each task owns live heap data and stack locals across every suspension. */
    unsigned char local[512];for (unsigned j=0;j<sizeof(local);++j) { local[j]=(unsigned char)(id*41+j); }
    unsigned char *buffer=kmalloc(17+id*32);if (!buffer) { task_exit(1); }owned[id]=buffer;
    size_t capacity=heap_usable_size(buffer);for (size_t j=0;j<capacity;++j) { buffer[j]=(unsigned char)(id+j+3); }
    bool good=task_context_selftest(12)==0;
    if (io_mode) {
        uint64_t owner=task_file_owner();
        good=kfile_open(owner,"/",io_paths[id],KFILE_READ|KFILE_WRITE|KFILE_CREATE,&io_handles[id])==FS_OK && good;
        good=kfile_open(owner,"/","/tmp/.task-io/shared",KFILE_READ,&io_shared[id])==FS_OK && good;
    }
    if (id==0) {
        /* Force asynchronous IRQ entry/return while this private task stack
         * is live, in addition to the procedure-call yield register test. */
        uint64_t before=irq_timer_delivered();
        good=irq_test_registers(timer_frequency()/40)==0 && good;
        private_irqs=irq_timer_delivered()-before;
        if (!private_irqs) { good=false; }
    }
    for (unsigned n=0;n<8 && good;++n) {
        for (unsigned j=0;j<sizeof(local);++j) { if (local[j]!=(unsigned char)(id*41+j)) { good=false; } }
        for (size_t j=0;j<capacity;++j) { if (buffer[j]!=(unsigned char)(id+j+3)) { good=false; } }
        if (!good) { break; }++checks[id];if (trace_count<sizeof(trace)/sizeof(trace[0])) { trace[trace_count++]=id; }
        if (io_mode) {
            uint64_t owner=task_file_owner();unsigned char c=(unsigned char)('a'+id+n),readback[8];size_t bytes;uint32_t at;
            good=kfile_write(owner,io_handles[id],&c,1,&bytes)==FS_OK && bytes==1;
            good=kfile_seek(owner,io_handles[id],0,KFILE_SET,&at)==FS_OK && good;
            good=kfile_read(owner,io_handles[id],readback,n+1,&bytes)==FS_OK && bytes==n+1 && good;
            for (unsigned j=0;j<=n && good;++j) { if (readback[j]!=(unsigned char)('a'+id+j)) { good=false; } }
            good=kfile_read(owner,io_shared[id],readback,1,&bytes)==FS_OK && bytes==1 && readback[0]==(unsigned char)"shared-io"[n] && good;
            good=vfs_write(io_paths[id],"blocked",7)==FS_BUSY && good;
            if (!good) { break; }
        }
        ++completed[id];
        if (!task_yield()) { good=false;break; }
        if (io_mode) {
            size_t bytes;unsigned char c;
            if (kfile_read(task_file_owner(),io_handles[(id+1)%3],&c,1,&bytes)!=FS_BAD_HANDLE || bytes) { good=false; }
        }
        if (!(n&1)) {
            uint64_t before=timer_ticks();if (!task_sleep_ms(10+id*10) || timer_ticks()-before<timer_frequency()*(10+id*10)/1000) { good=false; }
        }
    }
    if (!kfree(buffer)) { good=false; }else { owned[id]=NULL; }clean(local,sizeof(local));
    if (!good || id==2) { task_exit(good ? 0 : 2); } /* explicit exit as well as implicit return */
}
static void entry(void)
{
    int id=queue.current;if (!active || id<0 || id>=TASK_LIMIT) { broken(); }
    irq_restore(initial_irq);worker((unsigned)id);task_exit(0);
}
static void information(void)
{
    console_write("Kernel tasks: cooperative EL1; ");console_write(active ? "active" : "monitor owns CPU");console_write(" runs=");console_u64(run_count);console_write(" last_result=");console_write(!attempted ? "not-run" : failed ? "failed" : "passed");
    console_write("\nswitches=");console_u64(queue.switches);console_write(" elapsed_counter_ticks=");console_u64(elapsed);console_write(" private_stack_timer_irqs=");console_u64(private_irqs);console_write(" stack_bytes=16384/task; guard_bytes=4096; limit=4\n");
    for (unsigned i=0;i<TASK_LIMIT;++i) {
        const struct task_record *t=&queue.tasks[i];if (t->status==TASK_UNUSED) { continue; }
        console_write("task=");console_u64(i);console_write(" state=");console_write(task_status_text(t->status));console_write(" dispatches=");console_u64(t->dispatches);console_write(" yields=");console_u64(t->yields);console_write(" sleeps=");console_u64(t->sleeps);console_write(" wakes=");console_u64(t->wakes);console_write(" completed=");console_u64(completed[i]);console_write(" stack_touched_bytes=");console_u64(highwater[i]);console_write(" exit=");console_write(t->exit_code==0 ? "0" : "error");console_write("\n");
    }
    console_write("No preemption, EL0, process isolation or background command execution. Timer IRQs continue; queue wakeups run in scheduler context.\n");
}
static void run_test(bool files)
{
    if (active) { console_write("Task test already running.\n");return; }
    if (run_count>=(1ULL<<59)-2 || (files && kfile_count()>KFILE_LIMIT-6)) { console_write("Task test refused: owner identity or file capacity exhausted.\n");return; }
    if (!mmu_enabled() || !irq_timer_running() || !mmu_dma_compatible() || !heap_check()) { console_write("Task test refused: run mmuinit, irqinit and timerstart first.\n");return; }
    uint64_t flags=irq_save();
    if (flags&0x80) { console_write("Task test refused: CPU IRQs must be unmasked.\n");return; }
    for (unsigned i=0;i<TASK_LIMIT;++i) {
        if (mmu_dma_buffer((uintptr_t)task_stacks[i],TASK_GUARD_BYTES) || !mmu_dma_buffer(bottom(i),TASK_STACK_BYTES)) { irq_restore(flags);console_write("Task test refused: stack guard/RW mappings missing.\n");return; }
    }
    char scratch_path[RAMFS_PATH_MAX];bool directory;
    if (files && vfs_resolve("/","/tmp/.task-io",scratch_path,&directory)!=FS_NOT_FOUND) { irq_restore(flags);console_write("Task I/O test refused: /tmp/.task-io already exists.\n");return; }
    struct page_stats pages=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    if (files) {
        enum fs_result r=vfs_mkdir("/tmp/.task-io");
        if (r==FS_OK) { r=vfs_write("/tmp/.task-io/shared","shared-io",9); }
        if (r!=FS_OK) { (void)vfs_remove("/tmp/.task-io/shared",false);(void)vfs_remove("/tmp/.task-io",true);irq_restore(flags);console_write("FAIL: task-I/O scratch preparation.\n");return; }
    }
    task_queue_init(&queue);trace_count=0;private_irqs=0;failed=false;initial_irq=flags;io_mode=files;auto_closed=0;
    for (unsigned i=0;i<TASK_LIMIT;++i) {
        contexts[i]=(struct task_context){0};owned[i]=NULL;completed[i]=checks[i]=highwater[i]=0;io_handles[i]=io_shared[i]=0;
        unsigned char *p=(unsigned char *)bottom(i);for (unsigned j=0;j<TASK_STACK_BYTES;++j) { p[j]=j<64 ? 0x6d : 0xa5; }
    }
    for (unsigned i=0;i<3;++i) { unsigned id;if (task_add(&queue,&id)!=TASK_OK || id!=i) { broken(); }contexts[i].sp=top(i);contexts[i].x[11]=(uintptr_t)entry; }
    attempted=active=true;++run_count;started=timer_ticks();irq_restore(flags);
    console_write("Starting three cooperative tasks with private guarded stacks, heap buffers and timed sleeps.\n");
    for (unsigned spins=0;spins<20000000;++spins) {
        flags=irq_save();uint64_t now=timer_ticks();
        if (!irq_timer_running() || now-started>timer_frequency()*5) { task_cancel_all(&queue);failed=true;irq_restore(flags);break; }
        int id=task_pick(&queue,now);
        if (id<0) { bool done=task_queue_done(&queue);irq_restore(flags);if (done) { break; }continue; }
        if (!stack_ok((unsigned)id,true)) { broken(); }
        task_context_switch(&monitor,&contexts[id]);
        if (queue.current!=-1 || !stack_ok((unsigned)id,true)) { broken(); }
        irq_restore(flags);
    }
    flags=irq_save();elapsed=timer_ticks()-started;
    if (!task_queue_done(&queue)) { task_cancel_all(&queue);failed=true; }
    for (unsigned i=0;i<3;++i) {
        const struct task_record *t=&queue.tasks[i];
        if (t->status!=TASK_EXITED || t->exit_code || completed[i]!=8 || checks[i]!=8 || t->sleeps!=4 || t->wakes!=4 || !stack_ok(i,true)) { failed=true; }
        unsigned char *p=(unsigned char *)bottom(i);unsigned first=64;while (first<TASK_STACK_BYTES && p[first]==0xa5) { ++first; }highwater[i]=TASK_STACK_BYTES-first;
    }
    /* The initial yield-only register loops must round-robin fairly. */
    if (trace_count!=24 || trace[0]!=0 || trace[1]!=1 || trace[2]!=2) { failed=true; }
    active=false;
    for (unsigned i=0;i<TASK_LIMIT;++i) { auto_closed+=kfile_close_owner(owner_for(i)); }
    if (files) {
        if (auto_closed!=6) { failed=true; }
        for (unsigned i=0;i<3;++i) {
            unsigned char bytes[8];size_t count=0;
            if (vfs_read(io_paths[i],0,bytes,sizeof(bytes),&count)!=FS_OK || count!=8) { failed=true; }
            for (unsigned n=0;n<count;++n) { if (bytes[n]!=(unsigned char)('a'+i+n)) { failed=true; } }
            if (kfile_read(owner_for(i),io_handles[i],bytes,1,&count)!=FS_BAD_HANDLE) { failed=true; }
            if (vfs_remove(io_paths[i],false)!=FS_OK) { failed=true; }
        }
        if (vfs_remove("/tmp/.task-io/shared",false)!=FS_OK || vfs_remove("/tmp/.task-io",true)!=FS_OK) { failed=true; }
        if (kfile_count()!=handles) { failed=true; }
    }
    for (unsigned i=0;i<TASK_LIMIT;++i) { if (owned[i]) { if (!kfree(owned[i])) { failed=true; }owned[i]=NULL; } }
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    if (!heap_check() || final.objects!=heap.objects || final.pages!=heap.pages || final.capacity_bytes!=heap.capacity_bytes || pages.allocated_pages!=after.allocated_pages || pages.free_pages!=after.free_pages) { failed=true; }
    for (unsigned i=0;i<TASK_LIMIT;++i) { clean((void *)bottom(i),TASK_STACK_BYTES);contexts[i]=(struct task_context){0}; }
    irq_restore(flags);
    console_write(failed ? "FAIL: cooperative task test; inspect taskinfo.\n" : "PASS: three guarded kernel tasks yielded/slept/exited; callee registers/stack/heap data preserved; PMM/heap counts restored.\n");
    if (files) { console_write(failed ? "FAIL: cooperative task file I/O.\n" : "PASS: three tasks kept independent owned file cursors across yields/sleeps; foreign access rejected, six handles auto-closed and memory restored.\n"); }
}
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
bool task_command(const char *line)
{
    if (same(line,"schedtest")) { run_test(false);return true; }if (same(line,"taskiotest")) { run_test(true);return true; }
    if (same(line,"taskinfo")) { information();return true; }return false;
}
