/* Original bounded single-CPU EL0 batches. Only lower-EL exception frames
 * switch; the kernel/driver stack is never preempted. */
#include "process.h"
#include "elf_app.h"
#include "user_mmu.h"
#include "user_abi.h"
#include "memory.h"
#include "mmu.h"
#include "kfile.h"
#include "vfs.h"
#include "heap.h"
#include "irq.h"
#include "cpu.h"
#include "console.h"
#include "terminal.h"
#include "sha256.h"
#include "byte_pipe.h"
#include "app_profile.h"
#include "user_heap.h"
#include "network_worker.h"
#include "udp.h"
#include "tcp.h"
#include "trust_time.h"
#include "rng200.h"
#include "crypto.h"
static unsigned char copy_bytes[VFS_FILE_MAX];
static unsigned char profile_bytes[APP_PROFILE_BYTES];
enum status { UNUSED,READY,SLEEPING,INPUT_WAIT,CHILD_WAIT,PIPE_READ_WAIT,PIPE_WRITE_WAIT,UDP_WAIT,
              IPC_SEND_WAIT,IPC_RECV_WAIT,SIGNAL_WAIT,EXITED,FAULTED,TIMED_OUT,CANCELLED };
struct process {
    struct user_tables *tables;
    struct user_mapping maps[USER_MAPPING_LIMIT];unsigned map_count;
    struct user_heap heap;
    struct uos_memory_pressure pressure;
    struct exception_frame context;
    uint64_t sp,owner,calls,dispatches,esr,far,pc;
    enum status status;int code;
    char path[RAMFS_PATH_MAX];
    char cwd[RAMFS_PATH_MAX];struct app_args args;
    struct app_environment environment;
    uint64_t wake,sleeps,wakes,execs;
    uint64_t input_address,lines;unsigned input_capacity;
    unsigned parent,wait_child;uint64_t deadline;
    uint64_t stdin_handle,stdout_handle;bool io_failed;
    uint64_t pipe_address;size_t pipe_bytes;unsigned char pipe_buffer[512];unsigned pipe_stage;
    uint64_t udp_handle,udp_address;size_t udp_capacity;
    struct uos_message inbox[UOS_IPC_DEPTH],outgoing;
    uint64_t ipc_peer,ipc_address,ipc_image;
    unsigned inbox_count,stop_signal;
    bool signal_pending,has_exception;
};
static struct process tasks[PROCESS_LIMIT+1];
static struct process *const service_task=&tasks[PROCESS_LIMIT];
static struct {
    struct app_args args;
    char cwd[RAMFS_PATH_MAX];
    struct uos_fault_info last;
    uint64_t retry_at,last_slice,launches,restarts;
    unsigned retries_left;
    bool enabled,slice,cleanup_failed;
} service;
/* Staging uses an independent PMM-backed root until exec commits. */
static struct process exec_stage;
static struct user_tables memory_stage;
struct vm_region {
    uint64_t token,creator,peer;
    uint64_t physical[USER_VM_REGION_PAGES];
    unsigned pages,peer_rights;
    bool peer_mapped;
};
static struct vm_region regions[USER_VM_REGIONS];
static uint64_t region_sequence;
static unsigned char image[APP_ELF_MAX_BYTES];
static bool running,last_clean,child_cleanup_failed;
static unsigned current,total;
static uint64_t epoch,kernel_root,started,switches,preemptions,yields,idles;
static uint64_t pid_sequence,children_spawned,children_reaped;static int child_last_code;
static bool interactive,input_dropping;
static unsigned budget_seconds,input_head,input_count;
static unsigned char input_queue[TERMINAL_BYTES];
static struct terminal_line input_line;
static struct {
    bool active,cancelled;
    unsigned parent,count,slots[UOS_PIPE_STAGES];
    struct byte_pipe rings[UOS_PIPE_STAGES-1];
} channel;
static uint64_t pipe_runs,pipe_completions,pipe_written,pipe_read,pipe_read_waits,pipe_write_waits;
static int pipe_left_code,pipe_right_code;
extern void process_arch_root(uint64_t root);
extern void process_arch_enter(const struct exception_frame *frame,uint64_t stack);
extern unsigned char process_arch_return[];
static void zero(void *p,size_t n) { volatile unsigned char *d=p;while (n--) { *d++=0; } }
static void copy(void *d,const void *s,size_t n) { unsigned char *a=d;const unsigned char *b=s;while (n--) { *a++=*b++; } }
static uint64_t ticks_ms(uint64_t ticks,uint64_t hz)
{ return hz ? (ticks/hz)*1000+(ticks%hz)*1000/hz : 0; }
static void close_sockets(uint64_t owner)
{ udp_close_owner(owner);tcp_close_owner(owner,ticks_ms(timer_ticks(),timer_frequency())); }
static size_t length(const char *s) { size_t n=0;while (s[n]) { ++n; }return n; }
static bool ancestor(const char *parent,const char *path)
{ size_t n=length(parent);for (size_t i=0;i<n;++i) { if (parent[i]!=path[i]) { return false; } }return n==1 || !path[n] || path[n]=='/'; }
static uint64_t user_sp(void) { uint64_t sp;__asm__ volatile("mrs %0, sp_el0" : "=r"(sp));return sp; }
static bool user_range(const struct process *p,uint64_t address,size_t bytes,bool write)
{
    if (!bytes) { return true; }if (address>=1ULL<<39 || bytes>(1ULL<<39)-address) { return false; }
    while (bytes) {
        struct user_translation t;if (!user_mmu_lookup(p->tables,address,&t) || (write && t.kind!=USER_PAGE_RW)) { return false; }
        size_t n=4096-(size_t)(address&4095);if (n>bytes) { n=bytes; }address+=n;bytes-=n;
    }return true;
}
static void user_copy(const struct process *p,uint64_t address,void *buffer,size_t bytes,bool to_user)
{
    unsigned char *b=buffer;
    while (bytes) {
        struct user_translation t;if (!user_mmu_lookup(p->tables,address,&t)) { exception_frame_fatal(&p->context); }
        size_t n=4096-(size_t)(address&4095);if (n>bytes) { n=bytes; }
        if (to_user) { copy((void *)(uintptr_t)t.physical,b,n); }else { copy(b,(const void *)(uintptr_t)t.physical,n); }
        address+=n;b+=n;bytes-=n;
    }
}
static bool allocate(struct process *p,uint64_t va,enum user_page_kind kind)
{
    if (p->map_count==USER_MAPPING_LIMIT) { return false; }uint64_t physical;
    if (!page_alloc(&physical)) { return false; }
    if (!mmu_dma_buffer(physical,4096)) { (void)page_free(physical);return false; }
    zero((void *)(uintptr_t)physical,4096);p->maps[p->map_count++]=(struct user_mapping){va,physical,kind};return true;
}
enum { TABLE_BACKING_PAGES=sizeof(struct user_tables)/4096 };
_Static_assert(sizeof(struct user_tables)%4096==0,"user table backing pages");
_Static_assert((unsigned)USER_VM_REGION_PAGES==(unsigned)UOS_VM_MAX_PAGES,"VM page limit ABI");
static bool vm_release(struct vm_region *region,uint64_t owner)
{
    if (!owner) { return true; }
    if (region->creator==owner) {
        region->creator=0;
        if (!region->peer_mapped) { region->peer=0; }
    }
    if (region->peer==owner) { region->peer=0;region->peer_mapped=false; }
    if (!region->token || region->creator || region->peer) { return true; }
    bool good=true;
    for (unsigned i=0;i<region->pages;++i) {
        zero((void *)(uintptr_t)region->physical[i],4096);
        if (!page_free(region->physical[i])) { good=false; }
    }
    zero(region,sizeof(*region));
    return good;
}
static bool release_memory(struct process *p)
{
    bool good=true;
    for (unsigned i=0;i<p->map_count;++i) {
        if (p->maps[i].virtual_address>=USER_VM_BASE) { continue; }
        zero((void *)(uintptr_t)p->maps[i].physical,4096);
        if (!page_free(p->maps[i].physical)) { good=false; }
    }
    p->map_count=0;
    if (p->tables) {
        uintptr_t physical=(uintptr_t)p->tables;
        zero(p->tables,sizeof(*p->tables));
        for (unsigned i=0;i<TABLE_BACKING_PAGES;++i) { if (!page_free(physical+i*4096)) { good=false; } }
        p->tables=NULL;
    }
    zero(&p->environment,sizeof(p->environment));
    zero(p->heap.runs,sizeof(p->heap.runs));p->heap.live=0;
    zero(p->pipe_buffer,sizeof(p->pipe_buffer));p->pipe_bytes=p->pipe_address=0;return good;
}
static bool release_vm(struct process *p)
{
    bool good=true;
    for (unsigned i=0;i<USER_VM_REGIONS;++i) {
        if (!vm_release(&regions[i],p->owner)) { good=false; }
    }
    return good;
}
static uint64_t ipc_escrow(const struct process *p) { return p->owner | (1ULL<<61); }
static void ipc_discard(struct process *p)
{
    if (p->inbox_count) { (void)kfile_close_owner(ipc_escrow(p)); }
    p->inbox_count=0;
    zero(p->inbox,sizeof(p->inbox));
    zero(&p->outgoing,sizeof(p->outgoing));
    p->ipc_peer=p->ipc_address=p->ipc_image=0;
}
static bool release(struct process *p)
{
    close_sockets(p->owner);ipc_discard(p);(void)kfile_close_owner(p->owner);
    bool good=release_memory(p);
    return release_vm(p) && good;
}
static bool load_at(struct process *p,const char *path,const char *cwd,const struct app_args *args,const struct app_environment *env,uint64_t owner)
{
    /* Keep process owners clear of the reserved kernel file-owner IDs. */
    if (!owner && pid_sequence>=(1ULL<<60)-1) { return false; }
    p->owner=owner ? owner : (1ULL<<63)|++pid_sequence;p->status=UNUSED;p->parent=p->wait_child=PROCESS_LIMIT;uint64_t h=0;uint32_t bytes=0;size_t n=0;
    char absolute[RAMFS_PATH_MAX];bool directory;
    enum fs_result r=vfs_resolve(cwd,path,absolute,&directory);
    if (r==FS_OK && directory) { r=FS_IS_DIR; }
    if (r==FS_OK) { r=kfile_open(UINT64_MAX-1,"/",absolute,KFILE_READ,&h); }
    if (r==FS_OK) { r=vfs_file_size(absolute,&bytes); }
    if (r==FS_OK && bytes>sizeof(image)) { r=FS_FILE_TOO_LARGE; }
    if (r==FS_OK) { r=kfile_read(UINT64_MAX-1,h,image,bytes,&n);if (r==FS_OK && n!=bytes) { r=FS_IO; } }
    if (h) { (void)kfile_close(UINT64_MAX-1,h); }
    if (r!=FS_OK) {
        console_write("App load failed: ");console_write(ramfs_error(r));console_write(": ");
        for (const unsigned char *s=(const unsigned char *)path;*s;++s) { console_putc(*s>=32 && *s<127 ? (char)*s : '.'); }
        console_write("\n");return false;
    }
    struct app_image_plan plan;enum app_elf_result er=app_elf_plan(image,bytes,&plan);
    if (er!=APP_ELF_OK) { console_write("ELF rejected: ");console_write(app_elf_error(er));console_write("\n");return false; }
    for (unsigned i=0;i<plan.count;++i) {
        const struct app_segment *s=&plan.segments[i];enum user_page_kind kind=s->flags&APP_PF_X ? USER_PAGE_RX : s->flags&APP_PF_W ? USER_PAGE_RW : USER_PAGE_RO;
        for (uint64_t at=0;at<s->memsz;at+=4096) {
            if (!allocate(p,s->vaddr+at,kind)) { return false; }
            size_t bytes_here=at<s->filesz ? (size_t)(s->filesz-at) : 0;if (bytes_here>4096) { bytes_here=4096; }
            copy((void *)(uintptr_t)p->maps[p->map_count-1].physical,image+s->offset+(bytes_here ? at : 0),bytes_here);
        }
    }
    for (unsigned i=0;i<USER_STACK_PAGES;++i) { if (!allocate(p,USER_STACK_BASE+(uint64_t)i*4096,USER_PAGE_RW)) { return false; } }
    uint64_t root;
    if (!page_alloc_run(TABLE_BACKING_PAGES,&root)) { return false; }
    if (!mmu_dma_buffer(root,sizeof(*p->tables))) {
        for (unsigned i=0;i<TABLE_BACKING_PAGES;++i) { if (!page_free(root+i*4096)) { child_cleanup_failed=true; } }
        return false;
    }
    p->tables=(void *)(uintptr_t)root;
    zero(p->tables,sizeof(*p->tables));
    enum user_mmu_result ur=user_mmu_build(p->tables,root,(const uint64_t *)(uintptr_t)kernel_root,p->maps,p->map_count);
    if (ur!=USER_MMU_OK) { console_write("User mapping failed: ");console_write(user_mmu_result_text(ur));console_write("\n");return false; }
    p->context=(struct exception_frame){.elr=plan.entry,.spsr=0x340};p->sp=USER_STACK_BASE+USER_STACK_PAGES*4096;p->status=READY;
    size_t i=0;do { p->path[i]=absolute[i]; } while (absolute[i++]);
    copy(p->cwd,cwd,length(cwd)+1);
    if (args) { p->args=*args; }
    else { p->args.count=1;p->args.used=(unsigned)length(absolute)+1;copy(p->args.bytes,absolute,p->args.used); }
    if (env) { p->environment=*env; }else { app_env_init(&p->environment); }
    return true;
}
static bool load(struct process *p,const char *path,const char *cwd,const struct app_args *args,const struct app_environment *env)
{ return load_at(p,path,cwd,args,env,0); }
static bool live(const struct process *p) { return p->status>=READY && p->status<EXITED; }
static unsigned scheduled_slots(void) { return service.enabled ? PROCESS_LIMIT+1 : total; }
_Static_assert((unsigned)PROCESS_LIMIT==(unsigned)UOS_PROCESS_MAX_V2,"process snapshot capacity");
_Static_assert(PROCESS_LIMIT==UOS_PIPE_STAGES+1,"pipeline and parent capacity");
static uint64_t process_pid(const struct process *p) { return p->owner ? (p->owner&~(1ULL<<63))+1 : 0; }
static unsigned public_state(const struct process *p)
{
    switch (p->status) {
    case READY:return p==&tasks[current] ? UOS_PROCESS_RUNNING : UOS_PROCESS_READY;
    case SLEEPING:return UOS_PROCESS_SLEEPING;case INPUT_WAIT:return UOS_PROCESS_INPUT;
    case CHILD_WAIT:return UOS_PROCESS_WAIT_CHILD;case PIPE_READ_WAIT:return UOS_PROCESS_PIPE_READ;
    case PIPE_WRITE_WAIT:return UOS_PROCESS_PIPE_WRITE;case UDP_WAIT:return UOS_PROCESS_UDP_WAIT;case EXITED:return UOS_PROCESS_EXITED;
    case IPC_SEND_WAIT:return UOS_PROCESS_IPC_SEND;case IPC_RECV_WAIT:return UOS_PROCESS_IPC_RECV;
    case SIGNAL_WAIT:return UOS_PROCESS_SIGNAL_WAIT;
    case FAULTED:return UOS_PROCESS_FAULTED;case TIMED_OUT:return UOS_PROCESS_TIMEOUT;
    case CANCELLED:return UOS_PROCESS_CANCELLED;default:return 0;
    }
}
static bool is_child(unsigned parent,const struct process *child)
{
    return parent<PROCESS_LIMIT && child->status!=UNUSED && child->parent==parent;
}
static struct process *owned_child(uint64_t pid)
{
    if (channel.active && current==channel.parent) { return NULL; }
    for (unsigned i=0;i<total;++i) {
        if (is_child(current,&tasks[i]) && process_pid(&tasks[i])==pid) { return &tasks[i]; }
    }
    return NULL;
}
static bool has_children(void)
{
    for (unsigned i=0;i<total;++i) { if (is_child(current,&tasks[i])) { return true; } }
    return false;
}
static struct uos_fault_info stop_info(const struct process *p)
{
    struct uos_fault_info out={.pid=process_pid(p),.code=(uint32_t)p->code,.signal=p->stop_signal};
    switch (p->status) {
    case EXITED:out.reason=UOS_STOP_EXIT;break;
    case TIMED_OUT:out.reason=UOS_STOP_TIMEOUT;break;
    case CANCELLED:out.reason=UOS_STOP_SIGNAL;break;
    case FAULTED:
        out.reason=p->has_exception ? UOS_STOP_FAULT : UOS_STOP_KERNEL;
        if (p->has_exception) {
            unsigned ec=(unsigned)(p->esr>>26);
            out.pc=p->pc;out.esr=p->esr;
            if ((ec==0x20 || ec==0x24) && !(p->esr&(1ULL<<10))) {
                out.flags=UOS_FAULT_FAR_VALID;out.far=p->far;
            }
        }
        break;
    default:break;
    }
    return out;
}
static int64_t process_control(struct process *p,struct exception_frame *f)
{
    if (f->x[8]==UOS_SYS_PROCESSES) {
        bool legacy=f->x[1]==sizeof(struct uos_process_table);
        size_t bytes=legacy ? sizeof(struct uos_process_table) : sizeof(struct uos_process_table_v2);
        if (f->x[1]!=bytes) { return UOS_ERROR; }
        if (!user_range(p,f->x[0],bytes,true)) { return UOS_EFAULT; }
        unsigned limit=legacy ? UOS_PROCESS_MAX : UOS_PROCESS_MAX_V2;
        struct uos_process_table_v2 table={0};
        table.version=legacy ? UOS_PROCESS_VERSION : UOS_PROCESS_VERSION_V2;
        table.caller_pid=process_pid(p);
        unsigned first=p==service_task ? PROCESS_LIMIT : 0;
        unsigned end=p==service_task ? PROCESS_LIMIT+1 : total;
        for (unsigned i=first;i<end;++i) {
            const struct process *source=&tasks[i];if (source->status==UNUSED) { continue; }
            if (table.count==limit) { break; }
            /* Keep one slot for the caller when filling the legacy table. */
            if (i<current && table.count==limit-1) { continue; }
            struct uos_process_entry *e=&table.entries[table.count++];e->pid=process_pid(source);
            if (source->parent<total && tasks[source->parent].status!=UNUSED) { e->parent_pid=process_pid(&tasks[source->parent]); }
            e->state=public_state(source);e->syscalls=source->calls;e->dispatches=source->dispatches;e->heap_pages=source->heap.live;
            if (!live(source)) { e->exit_code=source->code; }
            e->flags=(i==current ? UOS_PROCESS_SELF : 0) | (source==owned_child(e->pid) ? UOS_PROCESS_CHILD : 0);
            size_t n=0;while (n+1<sizeof(e->path) && source->path[n]) { e->path[n]=source->path[n];++n; }
        }
        user_copy(p,f->x[0],&table,bytes,true);return 0;
    }
    struct process *child=owned_child(f->x[0]);if (!child) { return UOS_ECHILD; }
    if (f->x[8]==UOS_SYS_CHILD_FAULT) {
        if (!user_range(p,f->x[1],sizeof(struct uos_fault_info),true)) { return UOS_EFAULT; }
        if (live(child)) { return UOS_EAGAIN; }
        struct uos_fault_info out=stop_info(child);
        user_copy(p,f->x[1],&out,sizeof(out),true);
        return 0;
    }
    if (f->x[8]==UOS_SYS_CHILD_CANCEL) {
        /* Preserve completed outcomes. Reaping remains explicit: retaining the
         * slot keeps poll idempotent and old PIDs cannot name a reused slot. */
        if (live(child)) { child->status=CANCELLED;child->code=130;child->stop_signal=UOS_SIGINT;close_sockets(child->owner); }return 0;
    }
    if (!user_range(p,f->x[1],sizeof(struct uos_child_info),true)) { return UOS_EFAULT; }
    struct uos_child_info result={0};result.pid=process_pid(child);result.state=public_state(child);result.finished=!live(child);
    if (result.finished) { result.exit_code=child->code; }user_copy(p,f->x[1],&result,sizeof(result),true);return 0;
}
static void input_clear(void)
{ input_count=input_head=0;input_dropping=false;zero(input_queue,sizeof(input_queue));zero(&input_line,sizeof(input_line)); }
static unsigned terminal_owner(void)
{
    if (!interactive || !live(&tasks[0])) { return PROCESS_LIMIT; }
    return tasks[0].status==CHILD_WAIT ? tasks[0].wait_child : 0;
}
static int64_t pipe_transfer(struct process *p,bool reading,bool *blocked)
{
    *blocked=false;size_t n=0;enum byte_pipe_result r;
    struct byte_pipe *ring=&channel.rings[p->pipe_stage-1-(reading ? 1u : 0u)];
    if (reading) {
        if (!user_range(p,p->pipe_address,p->pipe_bytes,true)) { return UOS_EFAULT; }
        r=byte_pipe_read(ring,p->pipe_buffer,p->pipe_bytes,&n);
    } else { r=byte_pipe_write(ring,p->pipe_buffer,p->pipe_bytes);n=p->pipe_bytes; }
    if (r==BYTE_PIPE_AGAIN) { *blocked=true;return 0; }
    if (r!=BYTE_PIPE_OK) { p->io_failed=true;return r==BYTE_PIPE_BROKEN ? UOS_EPIPE : UOS_ERROR; }
    if (reading) { user_copy(p,p->pipe_address,p->pipe_buffer,n,true);pipe_read+=n; }else { pipe_written+=n; }
    return (int64_t)n;
}
static void pipeline_cancel(void)
{
    for (unsigned i=0;i<channel.count;++i) {
        struct process *p=&tasks[channel.slots[i]];
        if (live(p)) { p->status=CANCELLED;p->code=130;p->stop_signal=UOS_SIGINT; }
    }
}
/* All pipe progress/collection runs under the canonical kernel root. */
static void pipeline_refresh(void)
{
    if (!channel.active) { return; }
    struct process *parent=&tasks[channel.parent];
    if (!live(parent)) { pipeline_cancel(); }
    for (unsigned i=0;i+1<channel.count;++i) {
        struct byte_pipe *ring=&channel.rings[i];
        if (!live(&tasks[channel.slots[i]]) && ring->writer) { byte_pipe_close_writer(ring); }
        if (!live(&tasks[channel.slots[i+1]]) && ring->reader) { byte_pipe_close_reader(ring); }
    }
    for (unsigned round=0;round<2;++round) {
        for (unsigned i=0;i<channel.count;++i) {
            struct process *p=&tasks[channel.slots[i]];
            if (p->status!=PIPE_READ_WAIT && p->status!=PIPE_WRITE_WAIT) { continue; }
            bool blocked;int64_t result=pipe_transfer(p,p->status==PIPE_READ_WAIT,&blocked);
            if (!blocked) { p->context.x[0]=(uint64_t)result;p->status=READY;p->pipe_bytes=0;zero(p->pipe_buffer,sizeof(p->pipe_buffer)); }
        }
    }
    for (unsigned i=0;i<channel.count;++i) { if (live(&tasks[channel.slots[i]])) { return; } }
    pipe_left_code=tasks[channel.slots[0]].code;pipe_right_code=tasks[channel.slots[channel.count-1]].code;
    int result=0;
    bool clean=true;
    for (unsigned i=0;i<channel.count;++i) {
        struct process *stage=&tasks[channel.slots[i]];
        if (stage->code) { result=stage->code; }
        if (!release(stage)) { clean=false; }
        stage->status=UNUSED;
    }
    if (channel.cancelled) { result=130; }
    if (!clean) { child_cleanup_failed=true;result=126; }
    children_reaped+=channel.count;child_last_code=result;++pipe_completions;
    if (live(parent)) { parent->context.x[0]=(uint64_t)result;parent->status=READY; }
    zero(&channel,sizeof(channel));input_clear();
}
static int collect_child(struct process *child)
{
    int code=child->code;
    if (!release(child)) { child_cleanup_failed=true;code=126; }
    child->status=UNUSED;++children_reaped;child_last_code=code;return code;
}
/* Called only with the kernel root active. Never clear/free active user tables. */
static void children_refresh(void)
{
    for (unsigned i=0;i<total;++i) {
        struct process *child=&tasks[i];
        unsigned owner=child->parent;
        if (child->status==UNUSED || owner>=total || (channel.active && owner==channel.parent)) { continue; }
        struct process *p=&tasks[owner];
        if (!live(p)) {
            if (live(child)) { child->status=CANCELLED;child->code=130;child->stop_signal=UOS_SIGINT; }
            (void)collect_child(child);
        } else if (p->status==CHILD_WAIT && p->wait_child==i && !live(child)) {
            p->context.x[0]=(uint64_t)collect_child(child);
            p->wait_child=PROCESS_LIMIT;p->status=READY;
            if (owner==0) { input_clear(); }
        }
    }
}
static void terminal_cancel(struct process *p,int code)
{
    p->status=CANCELLED;p->code=code;p->stop_signal=code==130 ? UOS_SIGINT : 0;input_clear();
    console_write(code==130 ? "^C\n" : "\nTerminal queue overflow; application cancelled.\n");
}
static void terminal_interrupt(struct process *p)
{
    if (!budget_seconds && p==&tasks[0] && !p->execs && p->status==INPUT_WAIT &&
        user_range(p,p->input_address,1,true)) {
        char empty='\0';
        user_copy(p,p->input_address,&empty,1,true);
        p->context.x[0]=0;
        p->input_address=0;
        p->status=READY;
        input_clear();
        console_write("^C\n");
        return;
    }
    terminal_cancel(p,130);
}
static void terminal_deliver(struct process *p,int ch)
{
    unsigned previous=input_line.length;enum terminal_action a=terminal_feed(&input_line,ch);
    if (a==TERM_ECHO) { console_putc((char)ch); }
    else if (a==TERM_ERASE) { console_write("\b \b"); }
    else if (a==TERM_CLEAR) { while (previous--) { console_write("\b \b"); } }
    else if (a==TERM_CANCEL) { terminal_interrupt(p); }
    else if (a==TERM_LINE || a==TERM_REJECT || a==TERM_EOF || a==TERM_RX_REJECT) {
        console_write(a==TERM_RX_REJECT ? "\nSerial receive error; input discarded.\n" : "\n");
        p->context.x[0]=(uint64_t)(a==TERM_EOF ? UOS_EOF : UOS_ERROR);
        if (a==TERM_LINE) {
            /* The blocked process cannot change its mappings while waiting.
             * Recheck before the final copy so future mapping changes fail closed. */
            if (user_range(p,p->input_address,input_line.length+1,true)) {
                user_copy(p,p->input_address,input_line.bytes,input_line.length+1,true);
                p->context.x[0]=input_line.length;++p->lines;
            } else { p->context.x[0]=(uint64_t)UOS_EFAULT; }
        }
        p->status=READY;p->input_address=0;zero(&input_line,sizeof(input_line));
    }
}
static void terminal_poll(void)
{
    if (channel.active) {
        if (!interactive || !live(&tasks[channel.parent])) { return; }
        for (unsigned i=0;i<32;++i) {
            int ch=console_getc();if (ch<0) { break; }if (ch!=3) { continue; }
            channel.cancelled=true;pipeline_cancel();
            input_clear();console_write("^C\n");break;
        }
        return;
    }
    unsigned owner=terminal_owner();if (owner>=total || !live(&tasks[owner])) { return; }struct process *p=&tasks[owner];
    unsigned work=0;
    while (p->status==INPUT_WAIT && input_count && work++<32) {
        int ch=input_queue[input_head];input_queue[input_head]=0;input_head=(input_head+1)%TERMINAL_BYTES;--input_count;terminal_deliver(p,ch);
    }
    /* Bounded polling on timer/idle scheduling points, never a blocking
     * UART read in exception context. Ctrl+C also stops non-reading loops. */
    for (unsigned i=0;i<32 && live(p);++i) {
        int ch=console_getc();
        if (ch==CONSOLE_NO_INPUT) { break; }
        if (ch==CONSOLE_RX_ERROR) {
            if (input_dropping) { terminal_cancel(p,75); }
            else if (p->status==INPUT_WAIT) { terminal_deliver(p,ch);input_clear(); }
            else { input_clear();console_write("\nSerial receive error; input discarded.\n"); }
            break;
        }
        if (ch==3) { terminal_interrupt(p);break; }
        if (input_dropping) { if (ch=='\r' || ch=='\n') { terminal_cancel(p,75); }continue; }
        if (p->status==INPUT_WAIT && !input_count) { terminal_deliver(p,ch); }
        else if (input_count<TERMINAL_BYTES) { input_queue[(input_head+input_count)%TERMINAL_BYTES]=(unsigned char)ch;++input_count; }
        else {
            input_dropping=true;input_count=input_head=0;
            if (ch=='\r' || ch=='\n') { terminal_cancel(p,75); }
        }
    }
}
static int64_t udp_read_to_user(const struct process *p,uint64_t handle,uint64_t address,size_t capacity)
{
    if (!user_range(p,address,capacity,true)) { return UOS_EFAULT; }
    unsigned char buffer[UOS_UDP_MAX_BYTES];
    int64_t result=udp_recv(p->owner,handle,buffer,capacity);
    if (result>0) { user_copy(p,address,buffer,(size_t)result,true);zero(buffer,(size_t)result); }
    return result;
}
static void udp_wait_ready(struct process *p,uint64_t now)
{
    int64_t result=udp_read_to_user(p,p->udp_handle,p->udp_address,p->udp_capacity);
    if (result==UOS_EAGAIN) {
        if ((int64_t)(now-p->wake)<0) { return; }
        result=UOS_ETIMEDOUT;
    }
    p->udp_handle=p->udp_address=p->udp_capacity=0;
    p->context.x[0]=(uint64_t)result;p->status=READY;
}
static struct process *ipc_peer(const struct process *p,uint64_t pid)
{
    if (p==service_task) { return NULL; }
    unsigned slot=(unsigned)(p-tasks);
    for (unsigned i=0;i<total;++i) {
        struct process *peer=&tasks[i];
        if (live(peer) && process_pid(peer)==pid && (p->parent==i || peer->parent==slot)) { return peer; }
    }
    return NULL;
}
static int64_t ipc_enqueue(struct process *p,struct process *peer,const struct uos_message *message)
{
    if (peer->inbox_count==UOS_IPC_DEPTH) { return UOS_EAGAIN; }
    struct uos_message entry={.peer=process_pid(p),.bytes=message->bytes,.rights=message->rights};
    if (message->handle) {
        enum fs_result r=kfile_dup_to(p->owner,message->handle,ipc_escrow(peer),message->rights,
                                     KFILE_CLOEXEC,&entry.handle);
        if (r!=FS_OK) { return r==FS_NO_HANDLES ? UOS_ENOSPC : UOS_EBADF; }
    }
    copy(entry.data,message->data,entry.bytes);
    peer->inbox[peer->inbox_count++]=entry;
    return 0;
}
static int64_t ipc_dequeue(struct process *p,uint64_t peer,uint64_t address)
{
    if (!user_range(p,address,sizeof(struct uos_message),true)) { return UOS_EFAULT; }
    for (unsigned i=0;i<p->inbox_count;++i) {
        struct uos_message *entry=&p->inbox[i];
        if (peer && entry->peer!=peer) { continue; }
        if (entry->handle && kfile_receive(ipc_escrow(p),entry->handle,p->owner)!=FS_OK) { return UOS_EBADF; }
        user_copy(p,address,entry,sizeof(*entry),true);
        --p->inbox_count;
        for (unsigned j=i;j<p->inbox_count;++j) { p->inbox[j]=p->inbox[j+1]; }
        p->inbox[p->inbox_count]=(struct uos_message){0};
        return 0;
    }
    return peer && !ipc_peer(p,peer) ? UOS_EPIPE : UOS_EAGAIN;
}
static uint64_t wait_until(uint64_t ms)
{ return timer_ticks()+(timer_frequency()*ms+999)/1000; }
static void wait_complete(struct process *p,int64_t result)
{
    p->context.x[0]=(uint64_t)result;
    p->status=READY;
    p->ipc_peer=p->ipc_address=p->ipc_image=0;
    zero(&p->outgoing,sizeof(p->outgoing));
    p->udp_handle=p->udp_address=p->udp_capacity=0;
    ++p->wakes;
}
static int64_t ipc_call(struct process *p,struct exception_frame *f)
{
    bool sending=f->x[8]==UOS_SYS_IPC_SEND;
    uint64_t ms=sending ? f->x[1] : f->x[2];
    if (ms>UOS_WAIT_MAX_MS) { return UOS_ERROR; }
    int64_t result;
    if (sending) {
        if (!user_range(p,f->x[0],sizeof(struct uos_message),false)) { return UOS_EFAULT; }
        struct uos_message message;
        user_copy(p,f->x[0],&message,sizeof(message),false);
        if (message.bytes>UOS_IPC_BYTES || (message.handle && (!message.rights ||
            (message.rights&~(UOS_READ|UOS_WRITE)))) || (!message.handle && message.rights)) { return UOS_ERROR; }
        if (message.handle && (message.handle==p->stdin_handle || message.handle==p->stdout_handle)) { return UOS_EBADF; }
        struct process *peer=ipc_peer(p,message.peer);
        if (!peer) { return UOS_EPIPE; }
        result=ipc_enqueue(p,peer,&message);
        if (result!=UOS_EAGAIN || !ms) { return result; }
        p->outgoing=message;
        p->ipc_image=peer->execs;
    } else {
        result=ipc_dequeue(p,f->x[0],f->x[1]);
        if (result!=UOS_EAGAIN || !ms) { return result; }
        p->ipc_peer=f->x[0];
        p->ipc_address=f->x[1];
    }
    p->status=sending ? IPC_SEND_WAIT : IPC_RECV_WAIT;
    p->wake=wait_until(ms);
    ++p->sleeps;
    return 0;
}
static int64_t signal_call(struct process *p,struct exception_frame *f)
{
    if (f->x[8]==UOS_SYS_SIGNAL_WAIT) {
        if (f->x[0]>UOS_WAIT_MAX_MS) { return UOS_ERROR; }
        if (p->signal_pending) { p->signal_pending=false;return UOS_SIGUSR1; }
        if (!f->x[0]) { return UOS_EAGAIN; }
        p->wake=wait_until(f->x[0]);p->status=SIGNAL_WAIT;++p->sleeps;return 0;
    }
    unsigned signal=(unsigned)f->x[1];
    if ((uint64_t)signal!=f->x[1] || (signal!=UOS_SIGINT && signal!=UOS_SIGTERM && signal!=UOS_SIGUSR1)) { return UOS_ERROR; }
    struct process *child=owned_child(f->x[0]);
    if (!child || !live(child)) { return UOS_ECHILD; }
    if (signal!=UOS_SIGUSR1) {
        child->status=CANCELLED;child->code=128+(int)signal;child->stop_signal=signal;
    } else if (child->status==SIGNAL_WAIT) {
        wait_complete(child,signal);
    } else {
        child->signal_pending=true;
        if (child->status==IPC_SEND_WAIT || child->status==IPC_RECV_WAIT || child->status==UDP_WAIT ||
            child->status==SLEEPING || child->status==INPUT_WAIT) {
            if (child->status==INPUT_WAIT && (unsigned)(child-tasks)==terminal_owner()) { input_clear(); }
            wait_complete(child,UOS_EINTR);
        }
    }
    return 0;
}
static void ipc_refresh(struct process *p,uint64_t now)
{
    int64_t result=UOS_EAGAIN;
    if (p->status==IPC_RECV_WAIT) {
        result=ipc_dequeue(p,p->ipc_peer,p->ipc_address);
    } else if (p->status==IPC_SEND_WAIT) {
        struct process *peer=ipc_peer(p,p->outgoing.peer);
        result=peer && peer->execs==p->ipc_image ? ipc_enqueue(p,peer,&p->outgoing) : UOS_EPIPE;
    } else if (p->status!=SIGNAL_WAIT) {
        return;
    }
    if (result!=UOS_EAGAIN || (int64_t)(now-p->wake)>=0) {
        wait_complete(p,result!=UOS_EAGAIN ? result : UOS_ETIMEDOUT);
    }
}
static bool service_load(void)
{
    zero(service_task,sizeof(*service_task));
    bool loaded=load(service_task,service.args.bytes,service.cwd,&service.args,NULL);
    zero(image,sizeof(image));
    if (loaded) {
        ++service.launches;
        __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
        return true;
    }
    if (!release(service_task)) { service.cleanup_failed=true; }
    zero(service_task,sizeof(*service_task));
    service.last=(struct uos_fault_info){.reason=UOS_STOP_KERNEL,.code=126};
    return false;
}
static void service_collect(void)
{
    service.last=stop_info(service_task);
    if (!release(service_task)) { service.cleanup_failed=true; }
    zero(service_task,sizeof(*service_task));
}
static void service_refresh(uint64_t now)
{
    if (!service.enabled) { return; }
    if (!irq_timer_running()) {
        if (live(service_task)) { service_task->status=FAULTED;service_task->code=126; }
        if (service_task->status!=UNUSED) { service_collect(); }
        service.enabled=false;
        return;
    }
    if (live(service_task)) { return; }
    if (service_task->status!=UNUSED) {
        service_collect();
        service.retry_at=wait_until(250);
    }
    if (!service.last.code || !service.retries_left || service.cleanup_failed) { service.enabled=false;return; }
    if ((int64_t)(now-service.retry_at)>=0) {
        --service.retries_left;
        ++service.restarts;
        if (!service_load()) { service.enabled=false; }
    }
}
static void refresh(void)
{
    uint64_t now=timer_ticks();bool expired=budget_seconds && now-started>=timer_frequency()*budget_seconds;
    for (unsigned i=0;i<scheduled_slots();++i) {
        struct process *p=&tasks[i];if (!live(p)) { continue; }
        if (!irq_timer_running()) { p->status=FAULTED;p->code=126; }
        else if ((p!=service_task && expired) || (p->deadline && (int64_t)(now-p->deadline)>=0)) { p->status=TIMED_OUT;p->code=124; }
        else if (p->status==SLEEPING && (int64_t)(now-p->wake)>=0) { p->status=READY;++p->wakes; }
        else if (p->status==UDP_WAIT) { udp_wait_ready(p,now); }
    }
    if (!service.slice) { terminal_poll(); }
    pipeline_refresh();
    children_refresh();
    for (unsigned i=0;i<scheduled_slots();++i) {
        struct process *p=&tasks[i];
        if (live(p)) { ipc_refresh(p,now); }
        else if (p->status!=UNUSED) { close_sockets(p->owner);if (p->inbox_count) { ipc_discard(p); } }
    }
    service_refresh(now);
}
static bool network_checkpoint;
static bool schedule(struct exception_frame *frame,bool timer)
{
    struct process *old=&tasks[current];old->context=*frame;old->context.spsr=(frame->spsr&0xf0000000ULL)|0x340;old->sp=user_sp();
    refresh();
    if (service.slice) { goto kernel; }
    if (service.enabled) {
        unsigned slot=0;
        while (slot<total && !live(&tasks[slot])) { ++slot; }
        if (slot==total) { goto kernel; }
    }
    if (timer && network_worker_due()) {
        network_checkpoint=true;
        goto kernel;
    }
    unsigned slots=scheduled_slots();
    for (unsigned step=1;step<=slots;++step) {
        unsigned next=(current+step)%slots;if (tasks[next].status!=READY) { continue; }
        if (next!=current) { ++switches;if (timer) { ++preemptions; } }
        current=next;struct process *p=&tasks[current];++p->dispatches;
        process_arch_root(p->tables->physical);__asm__ volatile("msr sp_el0, %0" :: "r"(p->sp) : "memory");*frame=p->context;return true;
    }
kernel:
    process_arch_root(kernel_root);frame->elr=(uintptr_t)process_arch_return;frame->spsr=0x3c5;return true;
}
static int64_t string_out(struct process *p,uint64_t address,uint64_t capacity,const char *text)
{
    size_t n=length(text);if (capacity>512 || capacity<=n) { return UOS_ERROR; }
    if (!user_range(p,address,n+1,true)) { return UOS_EFAULT; }
    user_copy(p,address,(void *)text,n+1,true);return (int64_t)n;
}
struct directory_query { uint64_t wanted,index;bool found,bad;struct uos_entry entry; };
static void directory_entry(const char *name,bool dir,size_t bytes,void *context)
{
    struct directory_query *q=context;if (q->index++!=q->wanted) { return; }
    size_t n=length(name);if (n>=sizeof(q->entry.name)) { q->bad=true;return; }
    q->found=true;q->entry.bytes=bytes;q->entry.type=dir ? UOS_DIRECTORY : UOS_FILE;copy(q->entry.name,name,n+1);
}
static int64_t path_call(struct process *p,struct exception_frame *f)
{
    uint64_t a=f->x[0],b=f->x[1],out=f->x[8]==UOS_SYS_READDIR ? f->x[3] : f->x[2];
    if (!b || b>=RAMFS_PATH_MAX) { return UOS_ERROR; }
    if (!user_range(p,a,(size_t)b,false)) { return UOS_EFAULT; }
    if (f->x[8]!=UOS_SYS_CHDIR && !user_range(p,out,sizeof(struct uos_entry),true)) { return UOS_EFAULT; }
    char requested[RAMFS_PATH_MAX],absolute[RAMFS_PATH_MAX];bool dir;
    user_copy(p,a,requested,(size_t)b,false);for (size_t i=0;i<b;++i) { if (!requested[i]) { return UOS_ERROR; } }requested[b]=0;
    if (vfs_resolve(p->cwd,requested,absolute,&dir)!=FS_OK) { return UOS_ERROR; }
    if (f->x[8]==UOS_SYS_CHDIR) { if (!dir) { return UOS_ERROR; }copy(p->cwd,absolute,length(absolute)+1);return 0; }
    struct directory_query q={0};
    if (f->x[8]==UOS_SYS_STAT) {
        uint32_t bytes=0;if (!dir && vfs_file_size(absolute,&bytes)!=FS_OK) { return UOS_ERROR; }
        q.entry.bytes=bytes;q.entry.type=dir ? UOS_DIRECTORY : UOS_FILE;
    } else {
        if (!dir || f->x[2]>=1024) { return UOS_ERROR; }q.wanted=f->x[2];
        if (vfs_list(absolute,directory_entry,&q)!=FS_OK || q.bad) { return UOS_ERROR; }
        if (!q.found) { return 0; }
    }
    user_copy(p,out,&q.entry,sizeof(q.entry),true);return f->x[8]==UOS_SYS_STAT ? 0 : 1;
}
static int64_t file_operation(struct process *p,struct exception_frame *f)
{
    bool copying=f->x[8]==UOS_SYS_COPY,dir;uint64_t a=f->x[0],b=f->x[1],c=f->x[2],d=f->x[3];
    if (!b || b>=RAMFS_PATH_MAX || (copying && (!d || d>=RAMFS_PATH_MAX))) { return UOS_ERROR; }
    if (!user_range(p,a,(size_t)b,false) || !user_range(p,c,copying ? (size_t)d : 32,!copying)) { return UOS_EFAULT; }
    char source[RAMFS_PATH_MAX],target[RAMFS_PATH_MAX],absolute[RAMFS_PATH_MAX];
    user_copy(p,a,source,(size_t)b,false);for (size_t i=0;i<b;++i) { if (!source[i]) { return UOS_ERROR; } }source[b]=0;
    if (copying) {
        user_copy(p,c,target,(size_t)d,false);for (size_t i=0;i<d;++i) { if (!target[i]) { return UOS_ERROR; } }target[d]=0;
        if (target[d-1]=='/' || vfs_resolve(p->cwd,target,absolute,&dir)!=FS_NOT_FOUND || vfs_prepare_write(p->cwd,target,absolute)!=FS_OK) { return UOS_ERROR; }
    }
    uint64_t h=0,verify=0;uint32_t bytes=0;unsigned char buffer[512],digest[32];size_t n;int64_t result=UOS_ERROR;
    char resolved[RAMFS_PATH_MAX];
    enum fs_result resolved_result=vfs_resolve(p->cwd,source,resolved,&dir);
    if (resolved_result==FS_NOT_FOUND) { return UOS_ENOENT; }
    if (resolved_result!=FS_OK || dir || vfs_file_size(resolved,&bytes)!=FS_OK || bytes>(copying ? VFS_FILE_MAX : 65536u)) { return UOS_ERROR; }
    if (kfile_open(p->owner,"/",resolved,KFILE_READ,&h)!=FS_OK) { return UOS_ERROR; }
    struct sha256 hash;sha256_init(&hash);
    for (uint32_t at=0;at<bytes;) {
        size_t wanted=bytes-at;if (wanted>sizeof(buffer)) { wanted=sizeof(buffer); }
        if (kfile_read(p->owner,h,buffer,wanted,&n)!=FS_OK || n!=wanted) { goto done; }
        if (copying) { copy(copy_bytes+at,buffer,n); }else { sha256_update(&hash,buffer,n); }at+=(uint32_t)n;
    }
    if (!copying) { sha256_final(&hash,digest);user_copy(p,c,digest,32,true);result=0;goto done; }
    /* Kernel calls are serialized with IRQs masked. Existence check, complete
     * source read and destination publication cannot race another process. */
    if (vfs_write(absolute,copy_bytes,bytes)!=FS_OK || kfile_open(p->owner,"/",absolute,KFILE_READ,&verify)!=FS_OK) { goto done; }
    for (uint32_t at=0;at<bytes;) {
        size_t wanted=bytes-at;if (wanted>sizeof(buffer)) { wanted=sizeof(buffer); }
        if (kfile_read(p->owner,verify,buffer,wanted,&n)!=FS_OK || n!=wanted) { goto done; }
        for (size_t i=0;i<n;++i) { if (buffer[i]!=copy_bytes[at+i]) { goto done; } }at+=(uint32_t)n;
    }
    result=bytes;
done:
    if (verify && kfile_close(p->owner,verify)!=FS_OK) { result=UOS_ERROR; }
    if (kfile_close(p->owner,h)!=FS_OK) { result=UOS_ERROR; }
    zero(copy_bytes,sizeof(copy_bytes));zero(buffer,sizeof(buffer));zero(digest,sizeof(digest));zero(&hash,sizeof(hash));return result;
}
static int64_t environment_call(struct process *p,struct exception_frame *f)
{
    uint64_t a=f->x[0],b=f->x[1],c=f->x[2],d=f->x[3];unsigned call=(unsigned)f->x[8];
    if (call==UOS_SYS_ENV_ENTRY) {
        if (a>=APP_ENV_LIMIT) { return UOS_ERROR; }
        if (!user_range(p,b,sizeof(struct app_env_entry),true)) { return UOS_EFAULT; }
        if (a>=p->environment.count) { return 0; }
        user_copy(p,b,&p->environment.entries[a],sizeof(struct app_env_entry),true);return 1;
    }
    if (!b || b>=APP_ENV_NAME_BYTES || (call==UOS_SYS_ENV_SET && d>=APP_ENV_VALUE_BYTES) ||
        (call==UOS_SYS_ENV_GET && (!d || d>APP_ENV_VALUE_BYTES))) { return UOS_ERROR; }
    if (!user_range(p,a,(size_t)b,false) ||
        ((call==UOS_SYS_ENV_GET || call==UOS_SYS_ENV_SET) && !user_range(p,c,(size_t)d,call==UOS_SYS_ENV_GET))) { return UOS_EFAULT; }
    char name[APP_ENV_NAME_BYTES],value[APP_ENV_VALUE_BYTES];user_copy(p,a,name,(size_t)b,false);
    for (size_t i=0;i<b;++i) { if (!name[i]) { return UOS_ERROR; } }name[b]=0;
    enum app_env_result result;size_t bytes=0;
    if (call==UOS_SYS_ENV_SET) {
        user_copy(p,c,value,(size_t)d,false);for (size_t i=0;i<d;++i) { if (!value[i]) { zero(value,sizeof(value));return UOS_ERROR; } }value[d]=0;
        result=app_env_set(&p->environment,name,value);
    } else if (call==UOS_SYS_ENV_UNSET) { result=app_env_unset(&p->environment,name); }
    else {
        result=app_env_get(&p->environment,name,value,(size_t)d,&bytes);
        if (result==APP_ENV_OK) { user_copy(p,c,value,bytes+1,true); }
    }
    zero(value,sizeof(value));
    if (result==APP_ENV_MISSING) { return UOS_ENOENT; }if (result==APP_ENV_FULL) { return UOS_ENOSPC; }
    return result==APP_ENV_OK ? (int64_t)bytes : UOS_ERROR;
}
static int64_t profile_call(struct process *p,struct exception_frame *f)
{
    uint64_t action=f->x[0],address=f->x[1],bytes=f->x[2];
    if ((action!=UOS_PROFILE_SAVE && action!=UOS_PROFILE_LOAD) || !bytes || bytes>=RAMFS_PATH_MAX) { return UOS_ERROR; }
    if (!user_range(p,address,(size_t)bytes,false)) { return UOS_EFAULT; }
    char requested[RAMFS_PATH_MAX],path[RAMFS_PATH_MAX];user_copy(p,address,requested,(size_t)bytes,false);
    for (size_t i=0;i<bytes;++i) { if (!requested[i]) { return UOS_ERROR; } }requested[bytes]=0;
    if (requested[bytes-1]=='/') { return UOS_ERROR; }
    bool directory;uint32_t size=0;uint64_t handle=0;size_t actual=0;struct app_environment next={0};
    enum fs_result r;int64_t result=UOS_ERROR;zero(profile_bytes,sizeof(profile_bytes));
    if (action==UOS_PROFILE_LOAD) {
        r=vfs_resolve(p->cwd,requested,path,&directory);if (r!=FS_OK) { goto done; }if (directory) { r=FS_IS_DIR;goto done; }
        r=vfs_file_size(path,&size);if (r!=FS_OK) { goto done; }if (!size || size>sizeof(profile_bytes)) { r=FS_CORRUPT;goto done; }
        r=kfile_open(p->owner,"/",path,KFILE_READ,&handle);if (r!=FS_OK) { goto done; }
        r=kfile_read(p->owner,handle,profile_bytes,size,&actual);
        enum fs_result closed=kfile_close(p->owner,handle);handle=0;if (r==FS_OK) { r=closed; }
        if (r!=FS_OK) { goto done; }if (actual!=size || !app_profile_decode(profile_bytes,size,&next)) { r=FS_CORRUPT;goto done; }
        p->environment=next;result=0;goto done;
    }
    r=vfs_prepare_write(p->cwd,requested,path);if (r!=FS_OK) { goto done; }
    const char *prefix="/data/";unsigned i=0;while (prefix[i] && prefix[i]==path[i]) { ++i; }
    if (prefix[i] || !path[i]) { r=FS_CROSS_DEVICE;goto done; }
    if (!app_profile_encode(&p->environment,profile_bytes,sizeof(profile_bytes),&actual)) { r=FS_CORRUPT;goto done; }
    r=vfs_write(path,profile_bytes,actual);if (r==FS_OK) { r=vfs_data_sync(); }if (r!=FS_OK) { goto done; }
    /* The existing data snapshot publishes all profile bytes in one commit.
     * Verify the whole readback; kernel filesystem work never yields. */
    r=vfs_file_size(path,&size);if (r!=FS_OK) { goto done; }if (size!=actual) { r=FS_IO;goto done; }
    for (uint32_t at=0;at<size;) {
        unsigned char block[512];size_t count=0,wanted=size-at;if (wanted>sizeof(block)) { wanted=sizeof(block); }
        r=vfs_read(path,at,block,wanted,&count);bool equal=r==FS_OK && count==wanted;
        for (size_t j=0;equal && j<count;++j) { if (block[j]!=profile_bytes[at+j]) { equal=false; } }zero(block,sizeof(block));
        if (!equal) { if (r==FS_OK) { r=FS_IO; }goto done; }at+=(uint32_t)count;
    }
    result=0;
done:
    if (handle) { (void)kfile_close(p->owner,handle); }
    zero(profile_bytes,sizeof(profile_bytes));zero(&next,sizeof(next));
    if (!result) { return 0; }
    switch (r) {
    case FS_NOT_FOUND:return UOS_ENOENT;case FS_BUSY:return UOS_EBUSY;case FS_READ_ONLY:return UOS_EROFS;
    case FS_CROSS_DEVICE:return UOS_EXDEV;case FS_IS_DIR:return UOS_EISDIR;default:return UOS_ERROR;
    }
}
static int64_t udp_call(struct process *p,struct exception_frame *f)
{
    uint64_t a=f->x[0],b=f->x[1],c=f->x[2],timeout=f->x[3];
    if (f->x[8]==UOS_SYS_UDP_OPEN) {
        struct uos_udp_peer endpoint;
        if (!user_range(p,a,sizeof(endpoint),false)) { return UOS_EFAULT; }
        user_copy(p,a,&endpoint,sizeof(endpoint),false);return udp_open(p->owner,&endpoint);
    }
    if (f->x[8]==UOS_SYS_UDP_CLOSE) { return udp_close(p->owner,a); }
    if (c>UOS_UDP_MAX_BYTES) { return UOS_EMSGSIZE; }
    uint64_t hz=timer_frequency(),now=timer_ticks();
    if (f->x[8]==UOS_SYS_UDP_SEND) {
        if (!user_range(p,b,(size_t)c,false)) { return UOS_EFAULT; }
        unsigned char buffer[UOS_UDP_MAX_BYTES];
        user_copy(p,b,buffer,(size_t)c,false);
        int64_t result=udp_send(p->owner,a,buffer,(size_t)c,ticks_ms(now,hz));
        zero(buffer,(size_t)c);return result;
    }
    if (timeout>UOS_UDP_MAX_WAIT_MS || (timeout && (!hz || hz>(UINT64_MAX-999)/UOS_UDP_MAX_WAIT_MS))) { return UOS_ERROR; }
    int64_t result=udp_read_to_user(p,a,b,(size_t)c);
    if (result==UOS_EAGAIN && timeout) {
        p->udp_handle=a;p->udp_address=b;p->udp_capacity=(size_t)c;p->wake=now+(hz*timeout+999)/1000;p->status=UDP_WAIT;
    }
    return result;
}
static int64_t tcp_call(struct process *p,struct exception_frame *f)
{
    uint64_t handle=f->x[0],address=f->x[1],bytes=f->x[2],call=f->x[8];
    if (call==UOS_SYS_TCP_STATUS) { return tcp_status(p->owner,handle); }
    if (call==UOS_SYS_TCP_OPEN) {
        struct uos_tcp_peer peer;
        if (!user_range(p,handle,sizeof(peer),false)) { return UOS_EFAULT; }
        user_copy(p,handle,&peer,sizeof(peer),false);return tcp_open(p->owner,&peer,ticks_ms(timer_ticks(),timer_frequency()));
    }
    if (call==UOS_SYS_TCP_CLOSE) { return tcp_close(p->owner,handle,ticks_ms(timer_ticks(),timer_frequency())); }
    if (bytes>UOS_TCP_MAX_BYTES) { return UOS_EMSGSIZE; }
    bool reading=call==UOS_SYS_TCP_READ;
    if (!user_range(p,address,(size_t)bytes,reading)) { return UOS_EFAULT; }
    unsigned char data[UOS_TCP_MAX_BYTES];
    if (!reading) { user_copy(p,address,data,(size_t)bytes,false); }
    int64_t r=reading ? tcp_read(p->owner,handle,data,(size_t)bytes) : tcp_write(p->owner,handle,data,(size_t)bytes);
    if (reading && r>0) { user_copy(p,address,data,(size_t)r,true); }
    zero(data,reading ? (r>0 ? (size_t)r : 0) : (size_t)bytes);return r;
}
static int64_t file_error(enum fs_result result,bool extended)
{ return extended && result==FS_BAD_HANDLE ? UOS_EBADF : UOS_ERROR; }

_Static_assert((int)UOS_EXCL==(int)KFILE_EXCL && (int)UOS_APPEND==(int)KFILE_APPEND &&
               (int)UOS_CLOEXEC==(int)KFILE_CLOEXEC && (int)UOS_FD_MAX==(int)KFILE_LIMIT,"file handle ABI");
static int64_t descriptor_call(struct process *p,struct exception_frame *f)
{
    uint64_t handle=f->x[0],arg=f->x[1],value=f->x[2];
    if (f->x[8]==UOS_SYS_FD_LIST) {
        if (!user_range(p,handle,sizeof(struct uos_fd_table),true)) { return UOS_EFAULT; }
        struct kfile_entry entries[KFILE_LIMIT];
        struct uos_fd_table out={0};
        out.count=(uint32_t)kfile_list_owner(p->owner,p->stdin_handle,p->stdout_handle,entries);
        for (unsigned i=0;i<out.count;++i) {
            const struct kfile_entry *e=&entries[i];
            out.entries[i]=(struct uos_fd_entry){e->token,e->status.bytes,e->status.offset,e->status.flags,e->descriptor_flags};
        }
        user_copy(p,handle,&out,sizeof(out),true);
        return 0;
    }
    if (handle==p->stdin_handle || handle==p->stdout_handle) { return UOS_EBADF; }
    if (f->x[8]==UOS_SYS_FD_DUP) {
        if (arg>(UOS_READ|UOS_WRITE) || (value && value!=UOS_CLOEXEC)) { return UOS_ERROR; }
        uint64_t duplicate;
        enum fs_result r=kfile_dup(p->owner,handle,(unsigned)arg,(unsigned)value,&duplicate);
        if (r==FS_NO_HANDLES) { return UOS_ENOSPC; }
        return r==FS_OK ? (int64_t)duplicate : file_error(r,true);
    }
    if (value>1 || (arg!=UOS_CLOEXEC && arg!=UOS_APPEND)) { return UOS_ERROR; }
    enum fs_result r=arg==UOS_CLOEXEC ? kfile_set_cloexec(p->owner,handle,value!=0) :
                                         kfile_set_append(p->owner,handle,value!=0);
    return r==FS_OK ? 0 : file_error(r,true);
}
static int64_t syscall(struct process *p,struct exception_frame *f)
{
    uint64_t a=f->x[0],b=f->x[1],c=f->x[2];unsigned char buffer[512];size_t count=0;enum fs_result r;
    switch (f->x[8]) {
    case UOS_SYS_STATFS: {
        if (!b || b>=RAMFS_PATH_MAX) { return UOS_ERROR; }
        if (!user_range(p,c,sizeof(struct uos_fs_info),true) || !user_range(p,a,(size_t)b,false)) { return UOS_EFAULT; }
        char path[RAMFS_PATH_MAX],absolute[RAMFS_PATH_MAX];user_copy(p,a,path,(size_t)b,false);
        for (size_t i=0;i<b;++i) { if (!path[i]) { return UOS_ERROR; } }path[b]=0;bool dir;
        r=vfs_resolve(p->cwd,path,absolute,&dir);if (r!=FS_OK) { return r==FS_NOT_FOUND ? UOS_ENOENT : UOS_ERROR; }
        struct vfs_volume_info info;r=vfs_volume_info(absolute,&info);if (r!=FS_OK) { return UOS_ERROR; }
        struct uos_fs_info out={info.max_write_bytes,info.type,info.version,info.nodes,info.used,info.flags};
        user_copy(p,c,&out,sizeof(out),true);return 0;
    }
    case UOS_SYS_PROCESSES:case UOS_SYS_CHILD_POLL:case UOS_SYS_CHILD_CANCEL:
    case UOS_SYS_CHILD_FAULT:return process_control(p,f);
    case UOS_SYS_ENV_PROFILE:return profile_call(p,f);
    case UOS_SYS_ENV_GET:case UOS_SYS_ENV_SET:case UOS_SYS_ENV_UNSET:case UOS_SYS_ENV_ENTRY:return environment_call(p,f);
    case UOS_SYS_RENAME:case UOS_SYS_PUBLISH: {
        uint64_t d=f->x[3];if (!b || b>=RAMFS_PATH_MAX || !d || d>=RAMFS_PATH_MAX) { return UOS_ERROR; }
        if (!user_range(p,a,(size_t)b,false) || !user_range(p,c,(size_t)d,false)) { return UOS_EFAULT; }
        char from[RAMFS_PATH_MAX],to[RAMFS_PATH_MAX];user_copy(p,a,from,(size_t)b,false);user_copy(p,c,to,(size_t)d,false);
        for (size_t i=0;i<b;++i) { if (!from[i]) { return UOS_ERROR; } }for (size_t i=0;i<d;++i) { if (!to[i]) { return UOS_ERROR; } }
        from[b]=0;to[d]=0;r=f->x[8]==UOS_SYS_PUBLISH ? vfs_publish(p->cwd,from,to) : vfs_rename(p->cwd,from,to);
        switch (r) {
        case FS_OK:return 0;case FS_NOT_FOUND:return UOS_ENOENT;case FS_EXISTS:return UOS_EEXIST;
        case FS_BUSY:return UOS_EBUSY;case FS_CROSS_DEVICE:return UOS_EXDEV;case FS_READ_ONLY:return UOS_EROFS;case FS_IS_DIR:return UOS_EISDIR;
        default:return UOS_ERROR;
        }
    }
    case UOS_SYS_COPY:case UOS_SYS_FILEHASH:return file_operation(p,f);
    case UOS_SYS_SYNC:return vfs_data_sync()==FS_OK ? 0 : UOS_ERROR;
    case UOS_SYS_ARGC:return p->args.count;
    case UOS_SYS_ARG:if (a>=p->args.count) { return UOS_ERROR; }return string_out(p,b,c,p->args.bytes+p->args.offset[a]);
    case UOS_SYS_GETCWD:return string_out(p,a,b,p->cwd);
    case UOS_SYS_GETPID:return (int64_t)process_pid(p);
    case UOS_SYS_STAT:case UOS_SYS_READDIR:case UOS_SYS_CHDIR:return path_call(p,f);
    case UOS_SYS_MKDIR:case UOS_SYS_REMOVE:case UOS_SYS_REPLACE: {
        if (!b || b>=RAMFS_PATH_MAX || (f->x[8]==UOS_SYS_REMOVE && c>1) || (f->x[8]==UOS_SYS_REPLACE && f->x[3]>512)) { return UOS_ERROR; }
        if (!user_range(p,a,(size_t)b,false) || (f->x[8]==UOS_SYS_REPLACE && !user_range(p,c,(size_t)f->x[3],false))) { return UOS_EFAULT; }
        char requested[RAMFS_PATH_MAX],absolute[RAMFS_PATH_MAX];bool dir;
        user_copy(p,a,requested,(size_t)b,false);for (size_t i=0;i<b;++i) { if (!requested[i]) { return UOS_ERROR; } }requested[b]=0;
        if (f->x[8]==UOS_SYS_REMOVE) {
            r=vfs_resolve(p->cwd,requested,absolute,&dir);
            if (r==FS_OK && c) {
                if (ancestor(absolute,filesystem_cwd())) { return UOS_ERROR; }
                for (unsigned i=0;i<scheduled_slots();++i) { if (live(&tasks[i]) && ancestor(absolute,tasks[i].cwd)) { return UOS_ERROR; } }
            }
            if (r==FS_OK) { r=vfs_remove(absolute,c!=0); }return r==FS_OK ? 0 : UOS_ERROR;
        }
        r=vfs_prepare_write(p->cwd,requested,absolute);if (r!=FS_OK) { return UOS_ERROR; }
        if (f->x[8]==UOS_SYS_MKDIR) { return vfs_mkdir(absolute)==FS_OK ? 0 : UOS_ERROR; }
        user_copy(p,c,buffer,(size_t)f->x[3],false);r=vfs_write(absolute,buffer,(size_t)f->x[3]);return r==FS_OK ? (int64_t)f->x[3] : UOS_ERROR;
    }
    case UOS_SYS_WRITE:
        if (p==service_task) { return UOS_ENOTTY; }
        if (b>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,a,(size_t)b,false)) { return UOS_EFAULT; }
        user_copy(p,a,buffer,(size_t)b,false);
        if (p->stdout_handle) {
            r=kfile_write(p->owner,p->stdout_handle,buffer,(size_t)b,&count);
            if (r!=FS_OK || count!=b) { p->io_failed=true;return UOS_ERROR; }return (int64_t)count;
        }
        for (size_t i=0;i<b;++i) { unsigned char ch=buffer[i];console_putc((ch>=32 && ch<127) || ch=='\n' || ch=='\r' || ch=='\t' ? (char)ch : '.'); }
        return (int64_t)b;
    case UOS_SYS_STDIN:
        if (b>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,a,(size_t)b,true)) { return UOS_EFAULT; }
        if (!p->stdin_handle) { return UOS_ENOTTY; }
        r=kfile_read(p->owner,p->stdin_handle,buffer,(size_t)b,&count);
        if (r!=FS_OK) { p->io_failed=true;return UOS_ERROR; }
        user_copy(p,a,buffer,count,true);return (int64_t)count;
    case UOS_SYS_TIME_MS: {
        return (int64_t)ticks_ms(timer_ticks(),timer_frequency());
    }
    case UOS_SYS_UTC_TIME: {
        if (!user_range(p,a,sizeof(struct uos_utc_time),true)) { return UOS_EFAULT; }
        struct utc_sample now;
        if (system_utc(&now)!=UTC_OK) { return UOS_ENOTIME; }
        struct uos_utc_time out={now.seconds,now.age,now.remaining};
        user_copy(p,a,&out,sizeof(out),true);return 0;
    }
    case UOS_SYS_TRUST_CHECK: {
        unsigned char digest[32];
        if (!user_range(p,a,sizeof(digest),false)) { return UOS_EFAULT; }
        user_copy(p,a,digest,sizeof(digest),false);
        switch (system_trust_check(digest,b,c)) {
        case TRUST_OK:return 0;
        case TRUST_NO_TIME:return UOS_ENOTIME;
        case TRUST_UNKNOWN:return UOS_ENOENT;
        case TRUST_CERT_TIME:return UOS_ECERTTIME;
        }
        return UOS_ERROR;
    }
    case UOS_SYS_RANDOM: {
        unsigned char sample[UOS_RANDOM_MAX];
        if (!b || b>sizeof(sample)) { return UOS_ERROR; }
        if (!user_range(p,a,(size_t)b,true)) { return UOS_EFAULT; }
        bool ok=platform_random_bytes(sample,(size_t)b)==BLOCK_OK;
        if (ok) { user_copy(p,a,sample,(size_t)b,true); }
        crypto_wipe(sample,sizeof(sample));
        return ok ? 0 : UOS_ENORANDOM;
    }
    case UOS_SYS_FD_DUP:case UOS_SYS_FD_SET:case UOS_SYS_FD_LIST:return descriptor_call(p,f);
    case UOS_SYS_OPEN:
        if (!b || b>=RAMFS_PATH_MAX || c>(UOS_READ|UOS_WRITE|UOS_CREATE|UOS_TRUNC|UOS_EXCL|UOS_APPEND|UOS_CLOEXEC)) { return UOS_ERROR; }
        if (!user_range(p,a,(size_t)b,false)) { return UOS_EFAULT; }
        user_copy(p,a,buffer,(size_t)b,false);for (size_t i=0;i<b;++i) { if (!buffer[i]) { return UOS_ERROR; } }buffer[b]=0;
        { uint64_t handle;r=kfile_open(p->owner,p->cwd,(const char *)buffer,(unsigned)c,&handle);
          if (r!=FS_OK) { return r==FS_EXISTS && (c&UOS_EXCL) ? UOS_EEXIST : UOS_ERROR; }
          if (handle>INT64_MAX) { (void)kfile_close(p->owner,handle);return UOS_ERROR; }return (int64_t)handle; }
    case UOS_SYS_FSTAT: {
        if (!user_range(p,b,sizeof(struct uos_file_info),true)) { return UOS_EFAULT; }
        struct kfile_status status;
        r=kfile_stat(p->owner,a,&status);if (r!=FS_OK) { return file_error(r,true); }
        struct uos_file_info out={.bytes=status.bytes,.offset=status.offset,.flags=status.flags};
        user_copy(p,b,&out,sizeof(out),true);return 0;
    }
    case UOS_SYS_READ:case UOS_SYS_PREAD: {
        bool positioned=f->x[8]==UOS_SYS_PREAD;
        if (c>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,b,(size_t)c,true)) { return UOS_EFAULT; }
        r=positioned ? kfile_pread(p->owner,a,buffer,(size_t)c,f->x[3],&count) :
                      kfile_read(p->owner,a,buffer,(size_t)c,&count);
        if (r!=FS_OK) { return file_error(r,positioned); }
        user_copy(p,b,buffer,count,true);return (int64_t)count;
    }
    case UOS_SYS_WRITE_FILE:case UOS_SYS_PWRITE: {
        bool positioned=f->x[8]==UOS_SYS_PWRITE;
        if (c>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,b,(size_t)c,false)) { return UOS_EFAULT; }
        user_copy(p,b,buffer,(size_t)c,false);
        r=positioned ? kfile_pwrite(p->owner,a,buffer,(size_t)c,f->x[3],&count) :
                      kfile_write(p->owner,a,buffer,(size_t)c,&count);
        return r==FS_OK ? (int64_t)count : file_error(r,positioned);
    }
    case UOS_SYS_FTRUNCATE:
        r=kfile_truncate(p->owner,a,b);return r==FS_OK ? 0 : file_error(r,true);
    case UOS_SYS_SEEK: {
        uint32_t offset;if (c>2) { return UOS_ERROR; }r=kfile_seek(p->owner,a,(int64_t)b,(enum kfile_whence)c,&offset);return r==FS_OK ? (int64_t)offset : UOS_ERROR;
    }
    case UOS_SYS_CLOSE:return kfile_close(p->owner,a)==FS_OK ? 0 : UOS_ERROR;
    default:return UOS_ERROR;
    }
}
static bool args_valid(const struct app_args *args)
{
    if (!args || !args->count || args->count>APP_ARG_LIMIT || !args->used || args->used>APP_ARG_BYTES || args->offset[0] || !args->bytes[0]) { return false; }
    unsigned end=0;
    for (unsigned i=0;i<args->count;++i) {
        if (args->offset[i]!=end) { return false; }
        while (end<args->used && args->bytes[end]) { ++end; }if (end==args->used) { return false; }++end;
    }
    return end==args->used;
}
static bool heap_allocate_page(void *context,uint64_t *physical)
{
    struct process *p=context;if (!page_alloc(physical)) { return false; }
    if (!mmu_dma_buffer(*physical,4096)) {
        if (!page_free(*physical)) { p->heap.failed=true;child_cleanup_failed=true; }return false;
    }
    zero((void *)(uintptr_t)*physical,4096);return true;
}
static bool heap_release_page(void *context,uint64_t physical)
{ (void)context;zero((void *)(uintptr_t)physical,4096);return page_free(physical); }
static bool heap_publish(void *context,const struct user_mapping *maps,unsigned count)
{
    struct process *p=context;
    enum user_mmu_result r=user_mmu_build(&memory_stage,(uintptr_t)p->tables->entries,(const uint64_t *)(uintptr_t)kernel_root,maps,count);
    if (r==USER_MMU_OK) { copy(p->tables,&memory_stage,sizeof(*p->tables));__asm__ volatile("dsb sy" ::: "memory"); }
    zero(&memory_stage,sizeof(memory_stage));return r==USER_MMU_OK;
}
static uint64_t region_address(const struct vm_region *region)
{ return USER_VM_BASE+(uint64_t)(region-regions)*USER_VM_REGION_PAGES*4096; }
static bool vm_fits(const struct process *p,unsigned pages)
{
    unsigned mapped=0;
    for (unsigned i=0;i<p->map_count;++i) { mapped+=p->maps[i].virtual_address>=USER_VM_BASE; }
    return pages<=USER_VM_MAPPED_PAGES-mapped;
}
static int64_t vm_attach(struct process *p,struct vm_region *region,enum user_page_kind kind)
{
    if (!vm_fits(p,region->pages)) { return UOS_ENOSPC; }
    uint64_t address=region_address(region);
    for (unsigned i=0;i<region->pages;++i) {
        p->maps[p->map_count+i]=(struct user_mapping){address+(uint64_t)i*4096,region->physical[i],kind};
    }
    if (!heap_publish(p,p->maps,p->map_count+region->pages)) { return UOS_ERROR; }
    p->map_count+=region->pages;
    return (int64_t)address;
}
static int64_t vm_create(struct process *p,struct exception_frame *f)
{
    uint64_t peer_pid=f->x[0],pages=f->x[1],rights=f->x[2],out=f->x[3];
    if (!pages || pages>USER_VM_REGION_PAGES ||
        (peer_pid ? (rights!=UOS_READ && rights!=(UOS_READ|UOS_WRITE)) : rights!=0)) { return UOS_ERROR; }
    if (!user_range(p,out,sizeof(uint64_t),true)) { return UOS_EFAULT; }
    struct process *peer=peer_pid ? owned_child(peer_pid) : NULL;
    if (peer_pid && (!peer || !live(peer))) { return UOS_ECHILD; }
    if (!vm_fits(p,(unsigned)pages) || region_sequence==UINT64_MAX) { return UOS_ENOSPC; }
    struct vm_region *region=NULL;
    for (unsigned i=0;i<USER_VM_REGIONS;++i) { if (!regions[i].token) { region=&regions[i];break; } }
    if (!region) { return UOS_ENOSPC; }
    region->token=++region_sequence;
    region->creator=p->owner;
    int64_t result;
    for (;region->pages<pages;++region->pages) {
        if (!heap_allocate_page(p,&region->physical[region->pages])) { break; }
    }
    result=region->pages==pages ? vm_attach(p,region,USER_PAGE_RW) : UOS_ENOMEM;
    if (result<0) {
        if (!vm_release(region,p->owner)) { child_cleanup_failed=true; }
        return result;
    }
    region->peer=peer ? peer->owner : 0;
    region->peer_rights=(unsigned)rights;
    user_copy(p,out,&region->token,sizeof(region->token),true);
    return result;
}
static int64_t vm_call(struct process *p,struct exception_frame *f)
{
    if (f->x[8]==UOS_SYS_VM_CREATE) { return vm_create(p,f); }
    struct vm_region *region=NULL;
    for (unsigned i=0;i<USER_VM_REGIONS;++i) {
        if (f->x[0] && regions[i].token==f->x[0]) { region=&regions[i];break; }
    }
    if (!region) { return UOS_ENOENT; }
    bool creator=region->creator==p->owner,peer=region->peer==p->owner;
    if (f->x[8]==UOS_SYS_VM_MAP) {
        if (!peer || region->peer_mapped || !ipc_peer(p,(region->creator&~(1ULL<<63))+1)) { return UOS_ENOENT; }
        int64_t address=vm_attach(p,region,region->peer_rights&UOS_WRITE ? USER_PAGE_RW : USER_PAGE_RO);
        if (address>0) { region->peer_mapped=true; }
        return address;
    }
    if (!creator && !(peer && region->peer_mapped)) { return UOS_ENOENT; }
    uint64_t address=region_address(region);
    struct user_mapping next[USER_MAPPING_LIMIT];
    unsigned count=0;
    for (unsigned i=0;i<p->map_count;++i) {
        uint64_t va=p->maps[i].virtual_address;
        if (va<address || va-address>=region->pages*4096ULL) { next[count++]=p->maps[i]; }
    }
    if (!heap_publish(p,next,count)) { return UOS_ERROR; }
    copy(p->maps,next,count*sizeof(next[0]));
    p->map_count=count;
    if (vm_release(region,p->owner)) { return 0; }
    child_cleanup_failed=true;p->status=FAULTED;p->code=126;
    return UOS_ERROR;
}
static int64_t memory_call(struct process *p,struct exception_frame *f)
{
    if (f->x[8]==UOS_SYS_MEMORY_PRESSURE) {
        if (!user_range(p,f->x[0],sizeof(struct uos_memory_pressure),true)) { return UOS_EFAULT; }
        struct uos_memory_pressure info=p->pressure;
        info.free_pages=USER_HEAP_PAGES-p->heap.live;
        info.largest_run=user_heap_largest_free_run(&p->heap);
        user_copy(p,f->x[0],&info,sizeof(info),true);return 0;
    }
    if (f->x[8]==UOS_SYS_MEMORY_INFO) {
        if (!user_range(p,f->x[0],sizeof(struct uos_memory_info),true)) { return UOS_EFAULT; }
        struct uos_memory_info info={4096,USER_HEAP_PAGES,p->heap.live,p->heap.peak,p->heap.allocations,p->heap.free_calls};
        user_copy(p,f->x[0],&info,sizeof(info),true);return 0;
    }
    struct user_heap_ops ops={heap_allocate_page,heap_release_page,heap_publish,p};enum user_heap_result r;uint64_t address=0;
    if (f->x[8]==UOS_SYS_ALLOC_PAGES) {
        if (!f->x[0] || f->x[0]>USER_HEAP_PAGES) { return UOS_ERROR; }
        r=user_heap_allocate(&p->heap,p->maps,&p->map_count,(unsigned)f->x[0],&ops,&address);
    } else if (f->x[8]==UOS_SYS_RESIZE_PAGES) {
        if (!f->x[1] || f->x[1]>USER_HEAP_PAGES) { return UOS_ERROR; }
        r=user_heap_resize(&p->heap,p->maps,&p->map_count,f->x[0],(unsigned)f->x[1],&ops);
    } else { r=user_heap_free(&p->heap,p->maps,&p->map_count,f->x[0],&ops); }
    if (r==USER_HEAP_CORRUPT) { child_cleanup_failed=true;p->status=FAULTED;p->code=126; }
    switch (r) {
    case USER_HEAP_QUOTA:++p->pressure.quota_failures;return UOS_ENOMEM;
    case USER_HEAP_FRAGMENTED:++p->pressure.fragmentation_failures;return UOS_ENOMEM;
    case USER_HEAP_NO_MEMORY:++p->pressure.backing_failures;return UOS_ENOMEM;
    case USER_HEAP_MAP_FAILED:++p->pressure.mapping_failures;break;
    default:break;
    }
    return r==USER_HEAP_OK ? (int64_t)address : UOS_ERROR;
}
static int64_t replace_image(struct process *p,uint64_t address,struct exception_frame *f)
{
    if (has_children()) { return UOS_EBUSY; }
    if (!user_range(p,address,sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args args;user_copy(p,address,&args,sizeof(args),false);
    if (!args_valid(&args) || p->execs==UINT64_MAX) { return UOS_ERROR; }
    zero(&exec_stage,sizeof(exec_stage));
    /* The kernel root is active. Old pages, handles and frame remain intact
     * until every ELF byte, allocation and new table has been prepared. */
    bool loaded=load_at(&exec_stage,args.bytes,p->cwd,&args,&p->environment,p->owner);
    zero(image,sizeof(image));
    if (!loaded) {
        if (!release_memory(&exec_stage)) { child_cleanup_failed=true; }
        zero(&exec_stage,sizeof(exec_stage));return UOS_ERROR;
    }
    exec_stage.parent=p->parent;exec_stage.deadline=p->deadline;
    if (!budget_seconds && p==&tasks[0] && !p->execs) {
        exec_stage.deadline=timer_ticks()+timer_frequency()*30;
    }
    exec_stage.stdin_handle=p->stdin_handle;exec_stage.stdout_handle=p->stdout_handle;exec_stage.io_failed=p->io_failed;
    exec_stage.pipe_stage=p->pipe_stage;
    exec_stage.calls=p->calls;exec_stage.dispatches=p->dispatches;
    exec_stage.sleeps=p->sleeps;exec_stage.wakes=p->wakes;exec_stage.lines=p->lines;exec_stage.execs=p->execs+1;
    exec_stage.heap.peak=p->heap.peak;exec_stage.heap.allocations=p->heap.allocations;exec_stage.heap.free_calls=p->heap.free_calls;
    exec_stage.pressure=p->pressure;
    /* No fallible load operation remains. An allocator corruption during
     * teardown is contained as a process fault, never a return to freed code. */
    if (!release_memory(p)) {
        child_cleanup_failed=true;p->status=FAULTED;p->code=126;
        (void)release_memory(&exec_stage);zero(&exec_stage,sizeof(exec_stage));return UOS_ERROR;
    }
    close_sockets(p->owner);
    ipc_discard(p);
    if (!release_vm(p)) { child_cleanup_failed=true; }
    (void)kfile_close_on_exec(p->owner);
    copy(p,&exec_stage,sizeof(*p));zero(&exec_stage,sizeof(exec_stage));
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    __asm__ volatile("msr sp_el0, %0" :: "r"(p->sp) : "memory");*f=p->context;return 0;
}
static int64_t streams_copy(struct process *p,uint64_t address,struct uos_stdio *io,char *output,bool append)
{
    if (!user_range(p,address,sizeof(*io),false)) { return UOS_EFAULT; }
    user_copy(p,address,io,sizeof(*io),false);
    unsigned in=0,out=0;while (in<sizeof(io->input) && io->input[in]) { ++in; }while (out<sizeof(io->output) && io->output[out]) { ++out; }
    if (in==sizeof(io->input) || out==sizeof(io->output) || (append && !out)) { return UOS_ERROR; }
    bool dir;
    if (out && (io->output[out-1]=='/' || (!append && vfs_resolve(p->cwd,io->output,output,&dir)!=FS_NOT_FOUND) ||
        vfs_prepare_write(p->cwd,io->output,output)!=FS_OK)) { return UOS_ERROR; }
    return 0;
}
static int64_t spawn(struct process *p,uint64_t address,uint64_t seconds,bool redirected,uint64_t streams,bool append)
{
    /* One generation of children. IDs never repeat when a slot is reused. */
    if (p==service_task || p->parent!=PROCESS_LIMIT || !seconds || seconds>30) { return UOS_ERROR; }
    /* Keep the original ordinary spawn quota. A pipeline reserves its
     * children together through the separate pipeline launch path. */
    unsigned children=0;
    for (unsigned i=0;i<total;++i) { children+=is_child(current,&tasks[i]); }
    if (children>=2) { return UOS_ERROR; }
    unsigned slot=0;while (slot<PROCESS_LIMIT && tasks[slot].status!=UNUSED) { ++slot; }if (slot==PROCESS_LIMIT) { return UOS_ERROR; }
    if (!user_range(p,address,sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args args;user_copy(p,address,&args,sizeof(args),false);if (!args_valid(&args)) { return UOS_ERROR; }
    struct uos_stdio io={0};char output[RAMFS_PATH_MAX];
    if (redirected) {
        int64_t result=streams_copy(p,streams,&io,output,append);if (result) { return result; }
    }
    unsigned flags=KFILE_WRITE|KFILE_CREATE|(append ? KFILE_APPEND : KFILE_EXCL);
    struct process *child=&tasks[slot];zero(child,sizeof(*child));
    if (!load(child,args.bytes,p->cwd,&args,&p->environment) ||
        (io.input[0] && kfile_open(child->owner,p->cwd,io.input,KFILE_READ,&child->stdin_handle)!=FS_OK) ||
        (io.output[0] && kfile_open(child->owner,"/",output,flags,&child->stdout_handle)!=FS_OK)) {
        if (!release(child)) { child_cleanup_failed=true; }zero(child,sizeof(*child));
        zero(image,sizeof(image));return UOS_ERROR;
    }
    zero(image,sizeof(image));child->parent=current;child->deadline=timer_ticks()+timer_frequency()*seconds;
    if (slot>=total) { total=slot+1; }++children_spawned;
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    return (int64_t)(child->owner&~(1ULL<<63))+1;
}
static int64_t pipeline_start(struct process *p,struct exception_frame *f)
{
    bool chain=f->x[8]==UOS_SYS_PIPE_CHAIN;
    uint64_t count=chain ? f->x[1] : 2;
    if (count<2 || count>UOS_PIPE_STAGES || (chain && f->x[4]>UOS_PIPE_APPEND) ||
        channel.active || p==service_task || p->parent!=PROCESS_LIMIT || has_children() || !f->x[2] || f->x[2]>30) { return UOS_ERROR; }
    bool append=chain ? f->x[4]==UOS_PIPE_APPEND : f->x[8]==UOS_SYS_PIPE_APPEND;
    if (append && !f->x[3]) { return UOS_ERROR; }
    struct app_args args[UOS_PIPE_STAGES];
    if (chain) {
        size_t bytes=(size_t)count*sizeof(args[0]);
        if (!user_range(p,f->x[0],bytes,false)) { return UOS_EFAULT; }
        user_copy(p,f->x[0],args,bytes,false);
    } else {
        for (unsigned i=0;i<2;++i) {
            if (!user_range(p,f->x[i],sizeof(args[i]),false)) { return UOS_EFAULT; }
            user_copy(p,f->x[i],&args[i],sizeof(args[i]),false);
        }
    }
    for (unsigned i=0;i<count;++i) { if (!args_valid(&args[i])) { return UOS_ERROR; } }
    struct uos_stdio io={0};char output[RAMFS_PATH_MAX];
    if (f->x[3]) { int64_t result=streams_copy(p,f->x[3],&io,output,append);if (result) { return result; } }
    unsigned flags=KFILE_WRITE|KFILE_CREATE|(append ? KFILE_APPEND : KFILE_EXCL);
    unsigned slots[UOS_PIPE_STAGES],found=0;
    for (unsigned i=0;i<PROCESS_LIMIT && found<count;++i) { if (tasks[i].status==UNUSED) { slots[found++]=i; } }
    if (found!=count) { return UOS_ERROR; }
    unsigned prepared=0;
    for (unsigned i=0;i<count;++i) {
        struct process *stage=&tasks[slots[i]];zero(stage,sizeof(*stage));++prepared;
        if (!load(stage,args[i].bytes,p->cwd,&args[i],&p->environment)) { goto fail; }
    }
    struct process *first=&tasks[slots[0]],*last=&tasks[slots[count-1]];
    if ((io.input[0] && kfile_open(first->owner,p->cwd,io.input,KFILE_READ,&first->stdin_handle)!=FS_OK) ||
        (io.output[0] && kfile_open(last->owner,"/",output,flags,&last->stdout_handle)!=FS_OK)) { goto fail; }
    uint64_t deadline=timer_ticks()+timer_frequency()*f->x[2];
    for (unsigned i=0;i<count;++i) {
        struct process *stage=&tasks[slots[i]];
        stage->parent=current;stage->deadline=deadline;stage->pipe_stage=i+1;channel.slots[i]=slots[i];
        if (i+1<count) { byte_pipe_init(&channel.rings[i]); }
    }
    zero(image,sizeof(image));channel.active=true;channel.parent=current;channel.count=(unsigned)count;p->status=CHILD_WAIT;
    if (slots[count-1]>=total) { total=slots[count-1]+1; }
    children_spawned+=count;++pipe_runs;input_clear();
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");return 0;
fail:
    for (unsigned i=0;i<prepared;++i) {
        struct process *stage=&tasks[slots[i]];
        if (!release(stage)) { child_cleanup_failed=true; }
        zero(stage,sizeof(*stage));
    }
    zero(image,sizeof(image));return UOS_ERROR;
}
static bool dispatch_exception(struct exception_frame *f,bool timer_tick,bool irq_ok)
{
    struct process *p=&tasks[current];
    if (f->vector==9) {
        if (!irq_ok) { for (unsigned i=0;i<scheduled_slots();++i) { if (live(&tasks[i])) { tasks[i].status=FAULTED;tasks[i].code=126; } }return schedule(f,true); }
        if (!timer_tick) { return true; }
        return schedule(f,true);
    }
    ++p->calls;
    if ((f->esr>>26)==0x15 && !(f->esr&0xffff)) {
        if (f->x[8]==UOS_SYS_ALLOC_PAGES || f->x[8]==UOS_SYS_FREE_PAGES || f->x[8]==UOS_SYS_MEMORY_INFO ||
            f->x[8]==UOS_SYS_RESIZE_PAGES || f->x[8]==UOS_SYS_MEMORY_PRESSURE) {
            f->x[0]=(uint64_t)memory_call(p,f);if (!live(p)) { return schedule(f,false); }return true;
        }
        if (f->x[8]>=UOS_SYS_VM_CREATE && f->x[8]<=UOS_SYS_VM_UNMAP) {
            f->x[0]=(uint64_t)vm_call(p,f);return live(p) || schedule(f,false);
        }
        if (f->x[8]==UOS_SYS_EXEC) {
            int64_t result=replace_image(p,f->x[0],f);
            if (!live(p)) { return schedule(f,false); }
            if (result) { f->x[0]=(uint64_t)result; }return true;
        }
        if (f->x[8]>=UOS_SYS_UDP_OPEN && f->x[8]<=UOS_SYS_UDP_CLOSE) {
            f->x[0]=(uint64_t)udp_call(p,f);return p->status!=UDP_WAIT || schedule(f,false);
        }
        if (f->x[8]>=UOS_SYS_TCP_OPEN && f->x[8]<=UOS_SYS_TCP_CLOSE) { f->x[0]=(uint64_t)tcp_call(p,f);return true; }
        if (f->x[8]==UOS_SYS_IPC_SEND || f->x[8]==UOS_SYS_IPC_RECV ||
            f->x[8]==UOS_SYS_SIGNAL_SEND || f->x[8]==UOS_SYS_SIGNAL_WAIT) {
            f->x[0]=(uint64_t)(f->x[8]<=UOS_SYS_IPC_RECV ? ipc_call(p,f) : signal_call(p,f));
            return p->status==READY || schedule(f,false);
        }
        if (f->x[8]==UOS_SYS_PIPE_RUN || f->x[8]==UOS_SYS_PIPE_APPEND || f->x[8]==UOS_SYS_PIPE_CHAIN) {
            int64_t result=pipeline_start(p,f);
            f->x[0]=(uint64_t)result;return result ? true : schedule(f,false);
        }
        if (channel.active && p->pipe_stage &&
            ((p->pipe_stage<channel.count && f->x[8]==UOS_SYS_WRITE) || (p->pipe_stage>1 && f->x[8]==UOS_SYS_STDIN))) {
            bool reading=f->x[8]==UOS_SYS_STDIN;uint64_t address=f->x[0],bytes=f->x[1];
            if (bytes>sizeof(p->pipe_buffer)) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
            if (!user_range(p,address,(size_t)bytes,reading)) { f->x[0]=(uint64_t)UOS_EFAULT;return true; }
            if (!bytes) { f->x[0]=0;return true; }
            p->pipe_address=address;p->pipe_bytes=(size_t)bytes;
            if (!reading) { user_copy(p,address,p->pipe_buffer,(size_t)bytes,false); }
            bool blocked;int64_t result=pipe_transfer(p,reading,&blocked);f->x[0]=(uint64_t)result;
            if (!blocked) { p->pipe_bytes=0;zero(p->pipe_buffer,sizeof(p->pipe_buffer));return true; }
            p->status=reading ? PIPE_READ_WAIT : PIPE_WRITE_WAIT;
            if (reading) { ++pipe_read_waits; }else { ++pipe_write_waits; }return schedule(f,false);
        }
        if (f->x[8]==UOS_SYS_SPAWN || f->x[8]==UOS_SYS_SPAWN_IO || f->x[8]==UOS_SYS_SPAWN_APPEND) {
            f->x[0]=(uint64_t)spawn(p,f->x[0],f->x[1],f->x[8]!=UOS_SYS_SPAWN,f->x[2],f->x[8]==UOS_SYS_SPAWN_APPEND);
            return true;
        }
        if (f->x[8]==UOS_SYS_WAIT) {
            struct process *child=owned_child(f->x[0]);
            if (!child) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
            if (!live(child)) { f->x[0]=(uint64_t)collect_child(child);return true; }
            p->wait_child=(unsigned)(child-tasks);p->status=CHILD_WAIT;f->x[0]=0;
            if (interactive && current==0) {
                input_clear();
                if (child->status==INPUT_WAIT) { (void)terminal_begin(&input_line,child->input_capacity); }
            }
            return schedule(f,false);
        }
        if (f->x[8]==UOS_SYS_EXIT) { p->status=EXITED;p->code=(int)(f->x[0]&255);if (!p->code && p->io_failed) { p->code=74; }(void)kfile_close_owner(p->owner);return schedule(f,false); }
        if (f->x[8]==UOS_SYS_YIELD) { ++yields;f->x[0]=0;return schedule(f,false); }
        if (f->x[8]==UOS_SYS_READLINE) {
            uint64_t address=f->x[0],capacity=f->x[1];
            /* A child may run between spawn and wait. Validate and block its
             * read now, but do not consume input until its parent hands over
             * the terminal. Each waiter retains its own buffer/capacity. */
            if (channel.active || p->stdin_handle || !interactive || (current!=0 && (p->parent!=0 || !live(&tasks[0])))) { f->x[0]=(uint64_t)UOS_ENOTTY;return true; }
            if (capacity<2 || capacity>TERMINAL_BYTES) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
            if (!user_range(p,address,(size_t)capacity,true)) { f->x[0]=(uint64_t)UOS_EFAULT;return true; }
            if (current==terminal_owner()) { (void)terminal_begin(&input_line,(unsigned)capacity); }
            p->input_address=address;p->input_capacity=(unsigned)capacity;p->status=INPUT_WAIT;f->x[0]=0;return schedule(f,false);
        }
        if (f->x[8]==UOS_SYS_SLEEP_MS) {
            uint64_t ms=f->x[0],hz=timer_frequency();
            if (ms>1000 || !hz || hz>(UINT64_MAX-999)/1000) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
            f->x[0]=0;if (!ms) { ++yields;return schedule(f,false); }
            p->wake=timer_ticks()+(hz*ms+999)/1000;p->status=SLEEPING;++p->sleeps;return schedule(f,false);
        }
        f->x[0]=(uint64_t)syscall(p,f);return true;
    }
    p->status=FAULTED;p->code=125;p->has_exception=true;p->esr=f->esr;p->far=f->far;p->pc=f->elr;
    (void)kfile_close_owner(p->owner);return schedule(f,false);
}
bool process_exception(struct exception_frame *f,bool timer_tick,bool irq_ok)
{
    if (!running || current>=scheduled_slots() || (f->spsr&15)!=0 || (f->vector!=8 && f->vector!=9)) { return false; }
    /* All lower-EL kernel work uses the verified identity root. This also
     * makes allocator/DMA policy checks valid and keeps active user tables
     * out of the child teardown path. User copies walk explicit private maps. */
    process_arch_root(kernel_root);bool handled=dispatch_exception(f,timer_tick,irq_ok);
    if (handled && !(f->spsr&15)) { process_arch_root(tasks[current].tables->physical); }
    return handled;
}
bool process_service_start(const struct app_args *args)
{
    if (!args_valid(args) || running || service.enabled || service.cleanup_failed ||
        !mmu_enabled() || !mmu_dma_compatible() || !irq_timer_running()) { return false; }
    uint64_t flags=irq_save();
    if (flags&0x80) { irq_restore(flags);return false; }
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(kernel_root));
    zero(&service,sizeof(service));
    service.args=*args;
    const char *cwd=filesystem_cwd();
    copy(service.cwd,cwd,length(cwd)+1);
    service.retries_left=2;
    service.last_slice=UINT64_MAX;
    service.enabled=service_load();
    irq_restore(flags);
    return service.enabled;
}
void process_service_stop(void)
{
    uint64_t flags=irq_save();
    if (service_task->status!=UNUSED) {
        service_task->status=CANCELLED;service_task->code=143;service_task->stop_signal=UOS_SIGTERM;
        service_collect();
    }
    service.enabled=false;
    irq_restore(flags);
}
void process_service_info(void)
{
    console_write("Service enabled=");console_write(service.enabled ? "yes" : "no");
    console_write(" active=");console_write(live(service_task) ? "yes" : "no");
    console_write(" launches=");console_u64(service.launches);
    console_write(" restarts=");console_u64(service.restarts);
    console_write(" last_reason=");console_u64(service.last.reason);
    console_write(" last_code=");console_u64(service.last.code);
    console_write(" cleanup=");console_write(service.cleanup_failed ? "failed" : "ok");
    if (live(service_task)) {
        console_write(" pid=");console_u64(process_pid(service_task));
        console_write(" path=");console_write(service_task->path);
    }
    if (service.last.reason==UOS_STOP_FAULT) {
        console_write(" ESR=");console_hex(service.last.esr);
        console_write(" PC=");console_hex(service.last.pc);
        if (service.last.flags&UOS_FAULT_FAR_VALID) { console_write(" FAR=");console_hex(service.last.far); }
    }
    console_write("\n");
}
void process_service_step(void)
{
    if (!service.enabled) { return; }
    uint64_t now=timer_ticks();
    if (now==service.last_slice && irq_timer_running()) { return; }
    service.last_slice=now;
    uint64_t flags=irq_save();
    service.slice=true;
    refresh();
    if (!(flags&0x80) && service_task->status==READY) {
        unsigned foreground=current;
        current=PROCESS_LIMIT;running=true;
        ++service_task->dispatches;
        process_arch_root(service_task->tables->physical);
        process_arch_enter(&service_task->context,service_task->sp);
        current=foreground;running=false;
    }
    service.slice=false;
    irq_restore(flags);
}
static unsigned service_pages(void)
{
    if (!service_task->tables) { return 0; }
    return TABLE_BACKING_PAGES+service_task->map_count;
}
void process_info(void)
{
    console_write("EL0 processes: limit=");console_u64(PROCESS_LIMIT);
    console_write("; private pages; timer preempts users only; runs=");console_u64(epoch);
    console_write(" switches=");console_u64(switches);console_write(" preemptions=");console_u64(preemptions);console_write(" yields=");console_u64(yields);
    console_write(" idle_waits=");console_u64(idles);
    console_write(" terminal=");console_write(interactive ? "yes" : "no");console_write(" budget_seconds=");
    if (interactive && !budget_seconds) { console_write("unlimited"); }else { console_u64(budget_seconds); }
    console_write(" children_started=");console_u64(children_spawned);console_write(" children_reaped=");console_u64(children_reaped);
    console_write(" last_child_code=");console_u64((unsigned)child_last_code);
    console_write(" cleanup=");console_write(last_clean ? "restored" : "not-run/failed");console_write("\n");
    console_write("Pipelines: started=");console_u64(pipe_runs);console_write(" completed=");console_u64(pipe_completions);
    console_write(" written=");console_u64(pipe_written);console_write(" read=");console_u64(pipe_read);
    console_write(" reader_waits=");console_u64(pipe_read_waits);console_write(" writer_waits=");console_u64(pipe_write_waits);
    console_write(" left_exit=");console_u64((unsigned)pipe_left_code);console_write(" right_exit=");console_u64((unsigned)pipe_right_code);console_write("\n");
    for (unsigned i=0;i<total;++i) {
        const struct process *p=&tasks[i];if (p->status==UNUSED) { continue; }console_write("process=");console_u64(i);console_write(" path=");console_write(p->path);
        console_write(" state=");console_write(p->status==EXITED ? "exited" : p->status==FAULTED ? "faulted" : p->status==TIMED_OUT ? "timeout" : p->status==CANCELLED ? "cancelled" : "not-run");
        console_write(" code=");console_u64((unsigned)p->code);console_write(" syscalls=");console_u64(p->calls);console_write(" dispatches=");console_u64(p->dispatches);
        console_write(" sleeps=");console_u64(p->sleeps);console_write(" wakes=");console_u64(p->wakes);
        console_write(" input_lines=");console_u64(p->lines);
        console_write(" execs=");console_u64(p->execs);
        console_write(" heap_live_pages=");console_u64(p->heap.live);console_write(" heap_peak_pages=");console_u64(p->heap.peak);
        console_write(" heap_allocations=");console_u64(p->heap.allocations);console_write(" heap_free_calls=");console_u64(p->heap.free_calls);
        if (p->has_exception) {
            struct uos_fault_info fault=stop_info(p);
            console_write(" ESR=");console_hex(fault.esr);console_write(" PC=");console_hex(fault.pc);
            if (fault.flags&UOS_FAULT_FAR_VALID) { console_write(" FAR=");console_hex(fault.far); }
        }
        console_write("\n");
    }
}
static int run(const char *const *paths,unsigned count,const struct app_args *args,bool terminal,unsigned seconds)
{
    if (!paths || !count || count>PROCESS_LIMIT || (terminal && count!=1) || running || !mmu_enabled() || !mmu_dma_compatible() || !irq_timer_running() || epoch>=(1ULL<<60)-2) {
        console_write("App run refused: require mmuinit, irqinit, timerstart and 1..5 valid paths.\n");return 2;
    }
    uint64_t flags=irq_save();if (flags&0x80) { irq_restore(flags);console_write("App run refused: CPU IRQs must be unmasked.\n");return 2; }
    struct page_stats before=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    unsigned original_service_pages=service_pages(),original_service_handles=kfile_owner_count(service_task->owner);
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(kernel_root));
    zero(tasks,PROCESS_LIMIT*sizeof(tasks[0]));++epoch;total=count;current=0;switches=preemptions=yields=idles=0;last_clean=false;bool loaded=true,clean=true;
    children_spawned=children_reaped=0;child_last_code=0;child_cleanup_failed=false;
    zero(&channel,sizeof(channel));pipe_runs=pipe_completions=pipe_written=pipe_read=pipe_read_waits=pipe_write_waits=0;pipe_left_code=pipe_right_code=0;
    interactive=terminal;budget_seconds=seconds;
    input_clear();
    for (unsigned i=0;i<count;++i) { if (!paths[i] || !load(&tasks[i],paths[i],filesystem_cwd(),args,NULL)) { loaded=false;break; } }
    zero(image,sizeof(image));
    if (loaded) {
        if (interactive && !budget_seconds) {
            console_write("Terminal assigned to EL0; shell stays open until exit.\n");
        } else if (interactive) {
            console_write("Terminal assigned to EL0; Ctrl+C cancels; session_seconds=");console_u64(budget_seconds);console_write("\n");
        }
        started=timer_ticks();running=true;network_checkpoint=false;
        __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
        for (;;) {
            network_worker_service();
            refresh();unsigned slots=scheduled_slots(),next=slots;bool pending=false;
            bool checkpoint=network_checkpoint;
            network_checkpoint=false;
            for (unsigned step=0;step<slots;++step) {
                unsigned i=checkpoint ? (current+step+1)%slots : step;
                if (i<total && live(&tasks[i])) { pending=true; }
                if (next==slots && tasks[i].status==READY) { next=i; }
            }
            if (!pending) { break; }
            if (next==slots) {
                /* Back on the kernel root/stack, outside exception dispatch.
                 * The periodic IRQ bounds the check-to-WFI wakeup race. */
                ++idles;irq_restore(flags);__asm__ volatile("wfi" ::: "memory");(void)irq_save();continue;
            }
            if (next!=current) { ++switches;if (checkpoint) { ++preemptions; } }current=next;++tasks[next].dispatches;
            process_arch_root(tasks[next].tables->physical);process_arch_enter(&tasks[next].context,tasks[next].sp);
        }
        running=false;
    }
    input_clear();
    bool success=loaded;
    for (unsigned i=0;i<total;++i) { if (tasks[i].status!=UNUSED && (tasks[i].status!=EXITED || tasks[i].code)) { success=false; }if (!release(&tasks[i])) { clean=false; } }
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    /* RAM files created by applications legitimately retain heap storage.
     * Account for that ownership; private process pages must all be freed. */
    unsigned final_service_pages=service_pages(),final_service_handles=kfile_owner_count(service_task->owner);
    last_clean=clean && !child_cleanup_failed && !service.cleanup_failed &&
        before.allocated_pages+final.pages+final_service_pages==after.allocated_pages+heap.pages+original_service_pages &&
        before.free_pages+heap.pages+original_service_pages==after.free_pages+final.pages+final_service_pages &&
        handles+final_service_handles==kfile_count()+original_service_handles && heap_check() && mmu_dma_compatible();
    irq_restore(flags);process_info();
    console_write(last_clean ? "App resources restored; monitor ready.\n" : "FAIL: application resource restoration.\n");
    if (vfs_data_failed()) { vfs_data_info();platform_storage_info(); }
    return !loaded ? 2 : success && last_clean ? 0 : 1;
}
int process_run(const char *const *paths,unsigned count) { return run(paths,count,NULL,false,2); }
static int exec_args(const struct app_args *args,bool terminal,unsigned seconds)
{
    if (!args_valid(args)) { return 2; }const char *paths[]={args->bytes};return run(paths,1,args,terminal,seconds);
}
int process_exec(const struct app_args *args) { return exec_args(args,false,2); }
int process_terminal(const struct app_args *args,unsigned seconds) { return seconds && seconds<=300 ? exec_args(args,true,seconds) : 2; }
int process_shell(void)
{
    struct app_args args;
    if (!app_args_parse("/boot/SHELL.ELF",&args)) { return 2; }
    return exec_args(&args,true,0);
}
bool process_test(void)
{
    struct page_stats pages=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    char path[RAMFS_PATH_MAX];bool dir;
    if (vfs_resolve("/","/tmp/user-note.txt",path,&dir)!=FS_NOT_FOUND) { console_write("App test refused: /tmp/user-note.txt already exists.\n");return false; }
    const char *normal[]={"/boot/HELLO.ELF","/boot/COUNTER.ELF","/boot/FILES.ELF"};
    int r=process_run(normal,3);bool good=r==0;
    /* FILES deliberately creates a RAM file; remove it before judging the
     * global baseline. process_run reports the app's live file separately. */
    if (vfs_remove("/tmp/user-note.txt",false)!=FS_OK) { good=false; }
    const char *guarded[]={"/boot/BADPTR.ELF","/boot/LEAK.ELF","/boot/FAULT.ELF"};
    r=process_run(guarded,3);good=r==1 && last_clean && tasks[0].status==EXITED && !tasks[0].code && tasks[1].status==EXITED && !tasks[1].code && tasks[2].status==FAULTED && good;
    const char *permissions[]={"/boot/KERN.ELF","/boot/TEXT.ELF","/boot/XSTACK.ELF"};
    r=process_run(permissions,3);good=r==1 && last_clean && good;
    for (unsigned i=0;i<3;++i) {
        unsigned ec=(unsigned)(tasks[i].esr>>26),fsc=(unsigned)tasks[i].esr&63;
        if (tasks[i].status!=FAULTED || ec!=(i==2 ? 0x20u : 0x24u) || fsc<13 || fsc>15) { good=false; }
    }
    const char *preempt[]={"/boot/BUSY.ELF","/boot/SPIN.ELF"};
    r=process_run(preempt,2);good=r==1 && last_clean && preemptions>0 && tasks[0].status==EXITED && !tasks[0].code && tasks[1].status==TIMED_OUT && good;
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    return good && heap_check() && heap.objects==final.objects && heap.pages==final.pages && heap.capacity_bytes==final.capacity_bytes &&
        pages.allocated_pages==after.allocated_pages && pages.free_pages==after.free_pages && handles==kfile_count();
}
bool process_tools_test(void)
{
    struct page_stats before=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    char cwd[RAMFS_PATH_MAX];copy(cwd,filesystem_cwd(),length(filesystem_cwd())+1);
    struct app_args args;bool good=app_args_parse("/boot/TOOLCHK.ELF \"hello world\" \"Jack's\" \"\"",&args);
    if (!good) { return false; }good=process_exec(&args)==0;
    const char *sleepers[]={"/boot/SLEEPER.ELF","/boot/SLEEPER.ELF","/boot/SLEEPER.ELF"};
    good=process_run(sleepers,3)==0 && idles>0 && good;
    for (unsigned i=0;i<3;++i) { if (tasks[i].sleeps!=3 || tasks[i].wakes!=3) { good=false; } }
    const char *mixed[]={"/boot/SLEEPER.ELF","/boot/BUSY.ELF"};
    good=process_run(mixed,2)==0 && tasks[0].sleeps==3 && tasks[0].wakes==3 && good;
    const char *timeout[]={"/boot/DOZER.ELF"};
    good=process_run(timeout,1)==1 && last_clean && tasks[0].status==TIMED_OUT && idles>0 && good;
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    for (unsigned i=0;cwd[i] || filesystem_cwd()[i];++i) { if (cwd[i]!=filesystem_cwd()[i]) { good=false;break; } }
    return good && heap_check() && handles==kfile_count() && heap.pages==final.pages && heap.objects==final.objects && heap.capacity_bytes==final.capacity_bytes &&
        before.allocated_pages==after.allocated_pages && before.free_pages==after.free_pages;
}
