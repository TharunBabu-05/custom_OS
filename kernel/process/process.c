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
static unsigned char copy_bytes[VFS_FILE_MAX];
static unsigned char profile_bytes[APP_PROFILE_BYTES];
enum status { UNUSED,READY,SLEEPING,INPUT_WAIT,CHILD_WAIT,PIPE_READ_WAIT,PIPE_WRITE_WAIT,EXITED,FAULTED,TIMED_OUT,CANCELLED };
struct process {
    struct user_tables tables;
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
    unsigned parent,child;uint64_t deadline;
    uint64_t stdin_handle,stdout_handle;bool io_failed;
    uint64_t pipe_address;size_t pipe_bytes;unsigned char pipe_buffer[512];
};
static struct process tasks[PROCESS_LIMIT];
/* Inactive candidate: prepare tables for the current slot's physical root.
 * This permits exec even when all three process slots are occupied. */
static struct process exec_stage;
static struct user_tables memory_stage;
static unsigned char image[APP_ELF_MAX_BYTES];
static bool running,last_clean,child_cleanup_failed;
static unsigned current,total;
static uint64_t epoch,kernel_root,started,switches,preemptions,yields,idles;
static uint64_t pid_sequence,children_spawned,children_reaped;static int child_last_code;
static bool interactive,input_dropping;
static unsigned budget_seconds,input_head,input_count;
static unsigned char input_queue[TERMINAL_BYTES];
static struct terminal_line input_line;
static struct { bool active,cancelled;unsigned parent,left,right;struct byte_pipe ring; } channel;
static uint64_t pipe_runs,pipe_completions,pipe_written,pipe_read,pipe_read_waits,pipe_write_waits;
static int pipe_left_code,pipe_right_code;
extern void process_arch_root(uint64_t root);
extern void process_arch_enter(const struct exception_frame *frame,uint64_t stack);
extern unsigned char process_arch_return[];
static void zero(void *p,size_t n) { volatile unsigned char *d=p;while (n--) { *d++=0; } }
static void copy(void *d,const void *s,size_t n) { unsigned char *a=d;const unsigned char *b=s;while (n--) { *a++=*b++; } }
static size_t length(const char *s) { size_t n=0;while (s[n]) { ++n; }return n; }
static bool ancestor(const char *parent,const char *path)
{ size_t n=length(parent);for (size_t i=0;i<n;++i) { if (parent[i]!=path[i]) { return false; } }return n==1 || !path[n] || path[n]=='/'; }
static uint64_t user_sp(void) { uint64_t sp;__asm__ volatile("mrs %0, sp_el0" : "=r"(sp));return sp; }
static bool user_range(const struct process *p,uint64_t address,size_t bytes,bool write)
{
    if (!bytes) { return true; }if (address>=1ULL<<39 || bytes>(1ULL<<39)-address) { return false; }
    while (bytes) {
        struct user_translation t;if (!user_mmu_lookup(&p->tables,address,&t) || (write && t.kind!=USER_PAGE_RW)) { return false; }
        size_t n=4096-(size_t)(address&4095);if (n>bytes) { n=bytes; }address+=n;bytes-=n;
    }return true;
}
static void user_copy(const struct process *p,uint64_t address,void *buffer,size_t bytes,bool to_user)
{
    unsigned char *b=buffer;
    while (bytes) {
        struct user_translation t;if (!user_mmu_lookup(&p->tables,address,&t)) { exception_frame_fatal(&p->context); }
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
static bool release_memory(struct process *p)
{
    bool good=true;
    for (unsigned i=0;i<p->map_count;++i) { zero((void *)(uintptr_t)p->maps[i].physical,4096);if (!page_free(p->maps[i].physical)) { good=false; } }
    p->map_count=0;zero(&p->tables,sizeof(p->tables));zero(&p->environment,sizeof(p->environment));
    zero(p->heap.runs,sizeof(p->heap.runs));p->heap.live=0;
    zero(p->pipe_buffer,sizeof(p->pipe_buffer));p->pipe_bytes=p->pipe_address=0;return good;
}
static bool release(struct process *p)
{ (void)kfile_close_owner(p->owner);return release_memory(p); }
static bool load_at(struct process *p,const char *path,const char *cwd,const struct app_args *args,const struct app_environment *env,uint64_t owner,uint64_t table_root)
{
    /* Keep process owners clear of the reserved kernel file-owner IDs. */
    if (!owner && pid_sequence>=(1ULL<<60)-1) { return false; }
    p->owner=owner ? owner : (1ULL<<63)|++pid_sequence;p->status=UNUSED;p->parent=p->child=PROCESS_LIMIT;uint64_t h=0;uint32_t bytes=0;size_t n=0;
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
    enum user_mmu_result ur=user_mmu_build(&p->tables,table_root ? table_root : (uintptr_t)p->tables.entries,(const uint64_t *)(uintptr_t)kernel_root,p->maps,p->map_count);
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
{ return load_at(p,path,cwd,args,env,0,0); }
static bool live(const struct process *p) { return p->status==READY || p->status==SLEEPING || p->status==INPUT_WAIT || p->status==CHILD_WAIT || p->status==PIPE_READ_WAIT || p->status==PIPE_WRITE_WAIT; }
_Static_assert((unsigned)PROCESS_LIMIT==(unsigned)UOS_PROCESS_MAX,"process snapshot capacity");
static uint64_t process_pid(const struct process *p) { return p->owner ? (p->owner&~(1ULL<<63))+1 : 0; }
static unsigned public_state(const struct process *p)
{
    switch (p->status) {
    case READY:return p==&tasks[current] ? UOS_PROCESS_RUNNING : UOS_PROCESS_READY;
    case SLEEPING:return UOS_PROCESS_SLEEPING;case INPUT_WAIT:return UOS_PROCESS_INPUT;
    case CHILD_WAIT:return UOS_PROCESS_WAIT_CHILD;case PIPE_READ_WAIT:return UOS_PROCESS_PIPE_READ;
    case PIPE_WRITE_WAIT:return UOS_PROCESS_PIPE_WRITE;case EXITED:return UOS_PROCESS_EXITED;
    case FAULTED:return UOS_PROCESS_FAULTED;case TIMED_OUT:return UOS_PROCESS_TIMEOUT;
    case CANCELLED:return UOS_PROCESS_CANCELLED;default:return 0;
    }
}
static struct process *owned_child(const struct process *p,uint64_t pid)
{
    if (p->child>=total || (channel.active && current==channel.parent)) { return NULL; }
    struct process *child=&tasks[p->child];
    return child->status!=UNUSED && child->parent==current && process_pid(child)==pid ? child : NULL;
}
static int64_t process_control(struct process *p,struct exception_frame *f)
{
    if (f->x[8]==UOS_SYS_PROCESSES) {
        if (f->x[1]!=sizeof(struct uos_process_table)) { return UOS_ERROR; }
        if (!user_range(p,f->x[0],sizeof(struct uos_process_table),true)) { return UOS_EFAULT; }
        struct uos_process_table table;zero(&table,sizeof(table));table.version=UOS_PROCESS_VERSION;table.caller_pid=process_pid(p);
        for (unsigned i=0;i<total;++i) {
            const struct process *source=&tasks[i];if (source->status==UNUSED) { continue; }
            struct uos_process_entry *e=&table.entries[table.count++];e->pid=process_pid(source);
            if (source->parent<total && tasks[source->parent].status!=UNUSED) { e->parent_pid=process_pid(&tasks[source->parent]); }
            e->state=public_state(source);e->syscalls=source->calls;e->dispatches=source->dispatches;e->heap_pages=source->heap.live;
            if (!live(source)) { e->exit_code=source->code; }
            e->flags=(i==current ? UOS_PROCESS_SELF : 0) | (source==owned_child(p,e->pid) ? UOS_PROCESS_CHILD : 0);
            size_t n=0;while (n+1<sizeof(e->path) && source->path[n]) { e->path[n]=source->path[n];++n; }
        }
        user_copy(p,f->x[0],&table,sizeof(table),true);return 0;
    }
    struct process *child=owned_child(p,f->x[0]);if (!child) { return UOS_ECHILD; }
    if (f->x[8]==UOS_SYS_CHILD_CANCEL) {
        /* Preserve completed outcomes. Reaping remains explicit: retaining the
         * slot keeps poll idempotent and old PIDs cannot name a reused slot. */
        if (live(child)) { child->status=CANCELLED;child->code=130; }return 0;
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
    return tasks[0].status==CHILD_WAIT ? tasks[0].child : 0;
}
static int64_t pipe_transfer(struct process *p,bool reading,bool *blocked)
{
    *blocked=false;size_t n=0;enum byte_pipe_result r;
    if (reading) {
        if (!user_range(p,p->pipe_address,p->pipe_bytes,true)) { return UOS_EFAULT; }
        r=byte_pipe_read(&channel.ring,p->pipe_buffer,p->pipe_bytes,&n);
    } else { r=byte_pipe_write(&channel.ring,p->pipe_buffer,p->pipe_bytes);n=p->pipe_bytes; }
    if (r==BYTE_PIPE_AGAIN) { *blocked=true;return 0; }
    if (r!=BYTE_PIPE_OK) { p->io_failed=true;return r==BYTE_PIPE_BROKEN ? UOS_EPIPE : UOS_ERROR; }
    if (reading) { user_copy(p,p->pipe_address,p->pipe_buffer,n,true);pipe_read+=n; }else { pipe_written+=n; }
    return (int64_t)n;
}
/* All pipe progress/collection runs under the canonical kernel root. */
static void pipeline_refresh(void)
{
    if (!channel.active) { return; }
    struct process *parent=&tasks[channel.parent],*left=&tasks[channel.left],*right=&tasks[channel.right];
    if (!live(parent)) {
        if (live(left)) { left->status=CANCELLED;left->code=130; }
        if (live(right)) { right->status=CANCELLED;right->code=130; }
    }
    if (!live(left) && channel.ring.writer) { byte_pipe_close_writer(&channel.ring); }
    if (!live(right) && channel.ring.reader) { byte_pipe_close_reader(&channel.ring); }
    for (unsigned round=0;round<2;++round) {
        struct process *peers[]={left,right};
        for (unsigned i=0;i<2;++i) {
            struct process *p=peers[i];if (p->status!=PIPE_READ_WAIT && p->status!=PIPE_WRITE_WAIT) { continue; }
            bool blocked;int64_t result=pipe_transfer(p,p->status==PIPE_READ_WAIT,&blocked);
            if (!blocked) { p->context.x[0]=(uint64_t)result;p->status=READY;p->pipe_bytes=0;zero(p->pipe_buffer,sizeof(p->pipe_buffer)); }
        }
    }
    if (live(left) || live(right)) { return; }
    pipe_left_code=left->code;pipe_right_code=right->code;
    int result=channel.cancelled ? 130 : right->code ? right->code : left->code;
    bool left_clean=release(left),right_clean=release(right);
    if (!left_clean || !right_clean) { child_cleanup_failed=true;result=126; }
    left->status=right->status=UNUSED;parent->child=PROCESS_LIMIT;children_reaped+=2;child_last_code=result;++pipe_completions;
    if (live(parent)) { parent->context.x[0]=(uint64_t)result;parent->status=READY; }
    zero(&channel,sizeof(channel));input_clear();
}
static int collect_child(struct process *parent)
{
    struct process *child=&tasks[parent->child];int code=child->code;
    if (!release(child)) { child_cleanup_failed=true;code=126; }
    child->status=UNUSED;parent->child=PROCESS_LIMIT;++children_reaped;child_last_code=code;return code;
}
/* Called only with the kernel root active. Never clear/free active user tables. */
static void children_refresh(void)
{
    for (unsigned i=0;i<total;++i) {
        struct process *p=&tasks[i];if ((channel.active && i==channel.parent) || p->parent!=PROCESS_LIMIT || p->child>=total) { continue; }
        struct process *child=&tasks[p->child];
        if (!live(p)) {
            if (live(child)) { child->status=CANCELLED;child->code=130; }
            (void)collect_child(p);
        } else if (p->status==CHILD_WAIT && !live(child)) {
            p->context.x[0]=(uint64_t)collect_child(p);p->status=READY;input_clear();
        }
    }
}
static void terminal_cancel(struct process *p,int code)
{
    p->status=CANCELLED;p->code=code;input_clear();
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
    else if (a==TERM_LINE || a==TERM_REJECT || a==TERM_EOF) {
        console_write("\n");p->context.x[0]=(uint64_t)(a==TERM_EOF ? UOS_EOF : UOS_ERROR);
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
            unsigned peers[]={channel.left,channel.right};channel.cancelled=true;
            for (unsigned j=0;j<2;++j) { if (live(&tasks[peers[j]])) { tasks[peers[j]].status=CANCELLED;tasks[peers[j]].code=130; } }
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
        int ch=console_getc();if (ch<0) { break; }
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
static void refresh(void)
{
    uint64_t now=timer_ticks();bool expired=budget_seconds && now-started>=timer_frequency()*budget_seconds;
    for (unsigned i=0;i<total;++i) {
        struct process *p=&tasks[i];if (!live(p)) { continue; }
        if (!irq_timer_running()) { p->status=FAULTED;p->code=126; }
        else if (expired || (p->deadline && (int64_t)(now-p->deadline)>=0)) { p->status=TIMED_OUT;p->code=124; }
        else if (p->status==SLEEPING && (int64_t)(now-p->wake)>=0) { p->status=READY;++p->wakes; }
    }
    terminal_poll();
    pipeline_refresh();
    children_refresh();
}
static bool schedule(struct exception_frame *frame,bool timer)
{
    struct process *old=&tasks[current];old->context=*frame;old->context.spsr=(frame->spsr&0xf0000000ULL)|0x340;old->sp=user_sp();
    refresh();
    for (unsigned step=1;step<=total;++step) {
        unsigned next=(current+step)%total;if (tasks[next].status!=READY) { continue; }
        if (next!=current) { ++switches;if (timer) { ++preemptions; } }
        current=next;struct process *p=&tasks[current];++p->dispatches;
        process_arch_root(p->tables.physical);__asm__ volatile("msr sp_el0, %0" :: "r"(p->sp) : "memory");*frame=p->context;return true;
    }
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
    case UOS_SYS_PROCESSES:case UOS_SYS_CHILD_POLL:case UOS_SYS_CHILD_CANCEL:return process_control(p,f);
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
                for (unsigned i=0;i<total;++i) { if (live(&tasks[i]) && ancestor(absolute,tasks[i].cwd)) { return UOS_ERROR; } }
            }
            if (r==FS_OK) { r=vfs_remove(absolute,c!=0); }return r==FS_OK ? 0 : UOS_ERROR;
        }
        r=vfs_prepare_write(p->cwd,requested,absolute);if (r!=FS_OK) { return UOS_ERROR; }
        if (f->x[8]==UOS_SYS_MKDIR) { return vfs_mkdir(absolute)==FS_OK ? 0 : UOS_ERROR; }
        user_copy(p,c,buffer,(size_t)f->x[3],false);r=vfs_write(absolute,buffer,(size_t)f->x[3]);return r==FS_OK ? (int64_t)f->x[3] : UOS_ERROR;
    }
    case UOS_SYS_WRITE:
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
        uint64_t ticks=timer_ticks(),hz=timer_frequency();return hz ? (int64_t)((ticks/hz)*1000+(ticks%hz)*1000/hz) : 0;
    }
    case UOS_SYS_OPEN:
        if (!b || b>=RAMFS_PATH_MAX || c>15) { return UOS_ERROR; }if (!user_range(p,a,(size_t)b,false)) { return UOS_EFAULT; }
        user_copy(p,a,buffer,(size_t)b,false);for (size_t i=0;i<b;++i) { if (!buffer[i]) { return UOS_ERROR; } }buffer[b]=0;
        { uint64_t handle;r=kfile_open(p->owner,p->cwd,(const char *)buffer,(unsigned)c,&handle);
          if (r!=FS_OK) { return UOS_ERROR; }if (handle>INT64_MAX) { (void)kfile_close(p->owner,handle);return UOS_ERROR; }return (int64_t)handle; }
    case UOS_SYS_READ:
        if (c>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,b,(size_t)c,true)) { return UOS_EFAULT; }
        r=kfile_read(p->owner,a,buffer,(size_t)c,&count);if (r!=FS_OK) { return UOS_ERROR; }
        user_copy(p,b,buffer,count,true);return (int64_t)count;
    case UOS_SYS_WRITE_FILE:
        if (c>sizeof(buffer)) { return UOS_ERROR; }if (!user_range(p,b,(size_t)c,false)) { return UOS_EFAULT; }
        user_copy(p,b,buffer,(size_t)c,false);r=kfile_write(p->owner,a,buffer,(size_t)c,&count);return r==FS_OK ? (int64_t)count : UOS_ERROR;
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
    enum user_mmu_result r=user_mmu_build(&memory_stage,(uintptr_t)p->tables.entries,(const uint64_t *)(uintptr_t)kernel_root,maps,count);
    if (r==USER_MMU_OK) { copy(&p->tables,&memory_stage,sizeof(p->tables));__asm__ volatile("dsb sy" ::: "memory"); }
    zero(&memory_stage,sizeof(memory_stage));return r==USER_MMU_OK;
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
    if (p->child!=PROCESS_LIMIT) { return UOS_EBUSY; }
    if (!user_range(p,address,sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args args;user_copy(p,address,&args,sizeof(args),false);
    if (!args_valid(&args) || p->execs==UINT64_MAX) { return UOS_ERROR; }
    zero(&exec_stage,sizeof(exec_stage));
    /* The kernel root is active. Old pages, handles and frame remain intact
     * until every ELF byte, allocation and new table has been prepared. */
    bool loaded=load_at(&exec_stage,args.bytes,p->cwd,&args,&p->environment,p->owner,(uintptr_t)p->tables.entries);
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
    copy(p,&exec_stage,sizeof(*p));zero(&exec_stage,sizeof(exec_stage));
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    __asm__ volatile("msr sp_el0, %0" :: "r"(p->sp) : "memory");*f=p->context;return 0;
}
static int64_t streams_copy(struct process *p,uint64_t address,struct uos_stdio *io,char *output)
{
    if (!user_range(p,address,sizeof(*io),false)) { return UOS_EFAULT; }
    user_copy(p,address,io,sizeof(*io),false);
    unsigned in=0,out=0;while (in<sizeof(io->input) && io->input[in]) { ++in; }while (out<sizeof(io->output) && io->output[out]) { ++out; }
    if (in==sizeof(io->input) || out==sizeof(io->output)) { return UOS_ERROR; }
    bool dir;
    if (out && (io->output[out-1]=='/' || vfs_resolve(p->cwd,io->output,output,&dir)!=FS_NOT_FOUND ||
        vfs_prepare_write(p->cwd,io->output,output)!=FS_OK)) { return UOS_ERROR; }
    return 0;
}
static int64_t spawn(struct process *p,uint64_t address,uint64_t seconds,bool redirected,uint64_t streams)
{
    /* One generation of children; one outstanding child per root process.
     * No inherited handles. IDs never repeat when a slot is reused. */
    if (p->parent!=PROCESS_LIMIT || p->child!=PROCESS_LIMIT || !seconds || seconds>30) { return UOS_ERROR; }
    if (!user_range(p,address,sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args args;user_copy(p,address,&args,sizeof(args),false);if (!args_valid(&args)) { return UOS_ERROR; }
    struct uos_stdio io={0};char output[RAMFS_PATH_MAX];
    if (redirected) {
        int64_t result=streams_copy(p,streams,&io,output);if (result) { return result; }
    }
    unsigned slot=0;while (slot<PROCESS_LIMIT && tasks[slot].status!=UNUSED) { ++slot; }if (slot==PROCESS_LIMIT) { return UOS_ERROR; }
    struct process *child=&tasks[slot];zero(child,sizeof(*child));
    if (!load(child,args.bytes,p->cwd,&args,&p->environment) ||
        (io.input[0] && kfile_open(child->owner,p->cwd,io.input,KFILE_READ,&child->stdin_handle)!=FS_OK) ||
        (io.output[0] && kfile_open(child->owner,"/",output,KFILE_WRITE|KFILE_CREATE,&child->stdout_handle)!=FS_OK)) {
        if (!release(child)) { child_cleanup_failed=true; }zero(child,sizeof(*child));child->parent=child->child=PROCESS_LIMIT;
        zero(image,sizeof(image));return UOS_ERROR;
    }
    zero(image,sizeof(image));child->parent=current;child->deadline=timer_ticks()+timer_frequency()*seconds;p->child=slot;
    if (slot>=total) { total=slot+1; }++children_spawned;
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    return (int64_t)(child->owner&~(1ULL<<63))+1;
}
static int64_t pipeline_start(struct process *p,struct exception_frame *f)
{
    if (channel.active || p->parent!=PROCESS_LIMIT || p->child!=PROCESS_LIMIT || !f->x[2] || f->x[2]>30) { return UOS_ERROR; }
    if (!user_range(p,f->x[0],sizeof(struct app_args),false) || !user_range(p,f->x[1],sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args left_args,right_args;user_copy(p,f->x[0],&left_args,sizeof(left_args),false);user_copy(p,f->x[1],&right_args,sizeof(right_args),false);
    if (!args_valid(&left_args) || !args_valid(&right_args)) { return UOS_ERROR; }
    struct uos_stdio io={0};char output[RAMFS_PATH_MAX];
    if (f->x[3]) { int64_t result=streams_copy(p,f->x[3],&io,output);if (result) { return result; } }
    unsigned slots[2],found=0;
    for (unsigned i=0;i<PROCESS_LIMIT && found<2;++i) { if (tasks[i].status==UNUSED) { slots[found++]=i; } }
    if (found!=2) { return UOS_ERROR; }
    struct process *left=&tasks[slots[0]],*right=&tasks[slots[1]];zero(left,sizeof(*left));zero(right,sizeof(*right));
    bool loaded=load(left,left_args.bytes,p->cwd,&left_args,&p->environment) && load(right,right_args.bytes,p->cwd,&right_args,&p->environment);
    if (!loaded || (io.input[0] && kfile_open(left->owner,p->cwd,io.input,KFILE_READ,&left->stdin_handle)!=FS_OK) ||
        (io.output[0] && kfile_open(right->owner,"/",output,KFILE_WRITE|KFILE_CREATE,&right->stdout_handle)!=FS_OK)) {
        bool a=release(left),b=release(right);if (!a || !b) { child_cleanup_failed=true; }
        zero(left,sizeof(*left));zero(right,sizeof(*right));left->parent=left->child=right->parent=right->child=PROCESS_LIMIT;
        zero(image,sizeof(image));return UOS_ERROR;
    }
    zero(image,sizeof(image));channel.active=true;channel.parent=current;channel.left=slots[0];channel.right=slots[1];byte_pipe_init(&channel.ring);
    left->parent=right->parent=current;left->deadline=right->deadline=timer_ticks()+timer_frequency()*f->x[2];p->child=slots[0];p->status=CHILD_WAIT;
    if (slots[1]>=total) { total=slots[1]+1; }children_spawned+=2;++pipe_runs;input_clear();
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");return 0;
}
static bool dispatch_exception(struct exception_frame *f,bool timer_tick,bool irq_ok)
{
    struct process *p=&tasks[current];
    if (f->vector==9) {
        if (!irq_ok) { for (unsigned i=0;i<total;++i) { if (live(&tasks[i])) { tasks[i].status=FAULTED;tasks[i].code=126; } }return schedule(f,true); }
        if (!timer_tick) { return true; }
        return schedule(f,true);
    }
    ++p->calls;
    if ((f->esr>>26)==0x15 && !(f->esr&0xffff)) {
        if (f->x[8]==UOS_SYS_ALLOC_PAGES || f->x[8]==UOS_SYS_FREE_PAGES || f->x[8]==UOS_SYS_MEMORY_INFO ||
            f->x[8]==UOS_SYS_RESIZE_PAGES || f->x[8]==UOS_SYS_MEMORY_PRESSURE) {
            f->x[0]=(uint64_t)memory_call(p,f);if (!live(p)) { return schedule(f,false); }return true;
        }
        if (f->x[8]==UOS_SYS_EXEC) {
            int64_t result=replace_image(p,f->x[0],f);
            if (!live(p)) { return schedule(f,false); }
            if (result) { f->x[0]=(uint64_t)result; }return true;
        }
        if (f->x[8]==UOS_SYS_PIPE_RUN) {
            int64_t result=pipeline_start(p,f);f->x[0]=(uint64_t)result;return result ? true : schedule(f,false);
        }
        if (channel.active && ((current==channel.left && f->x[8]==UOS_SYS_WRITE) || (current==channel.right && f->x[8]==UOS_SYS_STDIN))) {
            bool reading=current==channel.right;uint64_t address=f->x[0],bytes=f->x[1];
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
        if (f->x[8]==UOS_SYS_SPAWN || f->x[8]==UOS_SYS_SPAWN_IO) { f->x[0]=(uint64_t)spawn(p,f->x[0],f->x[1],f->x[8]==UOS_SYS_SPAWN_IO,f->x[2]);return true; }
        if (f->x[8]==UOS_SYS_WAIT) {
            if (!owned_child(p,f->x[0])) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
            if (!live(&tasks[p->child])) { f->x[0]=(uint64_t)collect_child(p);return true; }
            p->status=CHILD_WAIT;f->x[0]=0;
            if (interactive && current==0) {
                input_clear();struct process *child=&tasks[p->child];
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
            if (channel.active || p->stdin_handle || !interactive || (current!=0 && (p->parent!=0 || tasks[0].child!=current || !live(&tasks[0])))) { f->x[0]=(uint64_t)UOS_ENOTTY;return true; }
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
    p->status=FAULTED;p->code=125;p->esr=f->esr;p->far=f->far;p->pc=f->elr;(void)kfile_close_owner(p->owner);return schedule(f,false);
}
bool process_exception(struct exception_frame *f,bool timer_tick,bool irq_ok)
{
    if (!running || current>=total || (f->spsr&15)!=0 || (f->vector!=8 && f->vector!=9)) { return false; }
    /* All lower-EL kernel work uses the verified identity root. This also
     * makes allocator/DMA policy checks valid and keeps active user tables
     * out of the child teardown path. User copies walk explicit private maps. */
    process_arch_root(kernel_root);bool handled=dispatch_exception(f,timer_tick,irq_ok);
    if (handled && !(f->spsr&15)) { process_arch_root(tasks[current].tables.physical); }
    return handled;
}
void process_info(void)
{
    console_write("EL0 processes: limit=3; private pages; timer preempts users only; runs=");console_u64(epoch);
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
        if (p->esr) { console_write(" ESR=");console_hex(p->esr);console_write(" FAR=");console_hex(p->far); }
        console_write("\n");
    }
}
static int run(const char *const *paths,unsigned count,const struct app_args *args,bool terminal,unsigned seconds)
{
    if (!paths || !count || count>PROCESS_LIMIT || (terminal && count!=1) || running || !mmu_enabled() || !mmu_dma_compatible() || !irq_timer_running() || epoch>=(1ULL<<60)-2) {
        console_write("App run refused: require mmuinit, irqinit, timerstart and 1..3 valid paths.\n");return 2;
    }
    uint64_t flags=irq_save();if (flags&0x80) { irq_restore(flags);console_write("App run refused: CPU IRQs must be unmasked.\n");return 2; }
    struct page_stats before=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(kernel_root));
    zero(tasks,sizeof(tasks));++epoch;total=count;current=0;switches=preemptions=yields=idles=0;last_clean=false;bool loaded=true,clean=true;
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
        started=timer_ticks();running=true;
        __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
        for (;;) {
            refresh();unsigned next=total;bool pending=false;
            for (unsigned i=0;i<total;++i) { if (live(&tasks[i])) { pending=true; }if (next==total && tasks[i].status==READY) { next=i; } }
            if (!pending) { break; }
            if (next==total) {
                /* Back on the kernel root/stack, outside exception dispatch.
                 * The periodic IRQ bounds the check-to-WFI wakeup race. */
                ++idles;irq_restore(flags);__asm__ volatile("wfi" ::: "memory");(void)irq_save();continue;
            }
            if (next!=current) { ++switches; }current=next;++tasks[next].dispatches;
            process_arch_root(tasks[next].tables.physical);process_arch_enter(&tasks[next].context,tasks[next].sp);
        }
        running=false;
    }
    input_clear();
    bool success=loaded;
    for (unsigned i=0;i<total;++i) { if (tasks[i].status!=UNUSED && (tasks[i].status!=EXITED || tasks[i].code)) { success=false; }if (!release(&tasks[i])) { clean=false; } }
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    /* RAM files created by applications legitimately retain heap storage.
     * Account for that ownership; private process pages must all be freed. */
    last_clean=clean && !child_cleanup_failed && before.allocated_pages+final.pages==after.allocated_pages+heap.pages && before.free_pages+heap.pages==after.free_pages+final.pages &&
        handles==kfile_count() && heap_check() && mmu_dma_compatible();
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
