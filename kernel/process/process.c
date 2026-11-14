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
static unsigned char copy_bytes[RAMFS_FILE_MAX];
enum status { UNUSED,READY,SLEEPING,INPUT_WAIT,CHILD_WAIT,EXITED,FAULTED,TIMED_OUT,CANCELLED };
struct process {
    struct user_tables tables;
    struct user_mapping maps[USER_MAPPING_LIMIT];unsigned map_count;
    struct exception_frame context;
    uint64_t sp,owner,calls,dispatches,esr,far,pc;
    enum status status;int code;
    char path[RAMFS_PATH_MAX];
    char cwd[RAMFS_PATH_MAX];struct app_args args;
    uint64_t wake,sleeps,wakes;
    uint64_t input_address,lines;unsigned input_capacity;
    unsigned parent,child;uint64_t deadline;
    uint64_t stdin_handle,stdout_handle;bool io_failed;
};
static struct process tasks[PROCESS_LIMIT];
static unsigned char image[APP_ELF_MAX_BYTES];
static bool running,last_clean,child_cleanup_failed;
static unsigned current,total;
static uint64_t epoch,kernel_root,started,switches,preemptions,yields,idles;
static uint64_t pid_sequence,children_spawned,children_reaped;static int child_last_code;
static bool interactive,input_dropping;
static unsigned budget_seconds,input_head,input_count;
static unsigned char input_queue[TERMINAL_BYTES];
static struct terminal_line input_line;
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
static bool release(struct process *p)
{
    bool good=true;(void)kfile_close_owner(p->owner);
    for (unsigned i=0;i<p->map_count;++i) { zero((void *)(uintptr_t)p->maps[i].physical,4096);if (!page_free(p->maps[i].physical)) { good=false; } }
    p->map_count=0;zero(&p->tables,sizeof(p->tables));return good;
}
static bool load(struct process *p,const char *path,const char *cwd,const struct app_args *args)
{
    /* Keep process owners clear of the reserved kernel file-owner IDs. */
    if (pid_sequence>=(1ULL<<60)-1) { return false; }
    p->owner=(1ULL<<63)|++pid_sequence;p->status=UNUSED;p->parent=p->child=PROCESS_LIMIT;uint64_t h=0;uint32_t bytes=0;size_t n=0;
    char absolute[RAMFS_PATH_MAX];bool directory;
    enum fs_result r=vfs_resolve(cwd,path,absolute,&directory);
    if (r==FS_OK && directory) { r=FS_IS_DIR; }
    if (r==FS_OK) { r=kfile_open(UINT64_MAX-1,"/",absolute,KFILE_READ,&h); }
    if (r==FS_OK) { r=vfs_file_size(absolute,&bytes); }
    if (r==FS_OK && bytes>sizeof(image)) { r=FS_FILE_TOO_LARGE; }
    if (r==FS_OK) { r=kfile_read(UINT64_MAX-1,h,image,bytes,&n);if (r==FS_OK && n!=bytes) { r=FS_IO; } }
    if (h) { (void)kfile_close(UINT64_MAX-1,h); }
    if (r!=FS_OK) { console_write("App load failed: ");console_write(ramfs_error(r));console_write("\n");return false; }
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
    enum user_mmu_result ur=user_mmu_build(&p->tables,(uintptr_t)p->tables.entries,(const uint64_t *)(uintptr_t)kernel_root,p->maps,p->map_count);
    if (ur!=USER_MMU_OK) { console_write("User mapping failed: ");console_write(user_mmu_result_text(ur));console_write("\n");return false; }
    p->context=(struct exception_frame){.elr=plan.entry,.spsr=0x340};p->sp=USER_STACK_BASE+USER_STACK_PAGES*4096;p->status=READY;
    size_t i=0;do { p->path[i]=absolute[i]; } while (absolute[i++]);
    copy(p->cwd,cwd,length(cwd)+1);
    if (args) { p->args=*args; }
    else { p->args.count=1;p->args.used=(unsigned)length(absolute)+1;copy(p->args.bytes,absolute,p->args.used); }
    return true;
}
static bool live(const struct process *p) { return p->status==READY || p->status==SLEEPING || p->status==INPUT_WAIT || p->status==CHILD_WAIT; }
static void input_clear(void)
{ input_count=input_head=0;input_dropping=false;zero(input_queue,sizeof(input_queue));zero(&input_line,sizeof(input_line)); }
static unsigned terminal_owner(void)
{
    if (!interactive || !live(&tasks[0])) { return PROCESS_LIMIT; }
    return tasks[0].status==CHILD_WAIT ? tasks[0].child : 0;
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
        struct process *p=&tasks[i];if (p->parent!=PROCESS_LIMIT || p->child>=total) { continue; }
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
    p->status=CANCELLED;p->code=code;input_count=input_head=0;input_dropping=false;
    zero(input_queue,sizeof(input_queue));zero(&input_line,sizeof(input_line));
    console_write(code==130 ? "^C\n" : "\nTerminal queue overflow; application cancelled.\n");
}
static void terminal_deliver(struct process *p,int ch)
{
    unsigned previous=input_line.length;enum terminal_action a=terminal_feed(&input_line,ch);
    if (a==TERM_ECHO) { console_putc((char)ch); }
    else if (a==TERM_ERASE) { console_write("\b \b"); }
    else if (a==TERM_CLEAR) { while (previous--) { console_write("\b \b"); } }
    else if (a==TERM_CANCEL) { terminal_cancel(p,130); }
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
    unsigned owner=terminal_owner();if (owner>=total || !live(&tasks[owner])) { return; }struct process *p=&tasks[owner];
    unsigned work=0;
    while (p->status==INPUT_WAIT && input_count && work++<32) {
        int ch=input_queue[input_head];input_queue[input_head]=0;input_head=(input_head+1)%TERMINAL_BYTES;--input_count;terminal_deliver(p,ch);
    }
    /* Bounded polling on timer/idle scheduling points, never a blocking
     * UART read in exception context. Ctrl+C also stops non-reading loops. */
    for (unsigned i=0;i<32 && live(p);++i) {
        int ch=console_getc();if (ch<0) { break; }
        if (ch==3) { terminal_cancel(p,130);break; }
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
    uint64_t now=timer_ticks();bool expired=now-started>=timer_frequency()*budget_seconds;
    for (unsigned i=0;i<total;++i) {
        struct process *p=&tasks[i];if (!live(p)) { continue; }
        if (!irq_timer_running()) { p->status=FAULTED;p->code=126; }
        else if (expired || (p->deadline && (int64_t)(now-p->deadline)>=0)) { p->status=TIMED_OUT;p->code=124; }
        else if (p->status==SLEEPING && (int64_t)(now-p->wake)>=0) { p->status=READY;++p->wakes; }
    }
    terminal_poll();
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
    if (vfs_resolve(p->cwd,source,resolved,&dir)!=FS_OK || dir || vfs_file_size(resolved,&bytes)!=FS_OK || bytes>(copying ? RAMFS_FILE_MAX : 65536u)) { return UOS_ERROR; }
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
static int64_t syscall(struct process *p,struct exception_frame *f)
{
    uint64_t a=f->x[0],b=f->x[1],c=f->x[2];unsigned char buffer[512];size_t count=0;enum fs_result r;
    switch (f->x[8]) {
    case UOS_SYS_COPY:case UOS_SYS_FILEHASH:return file_operation(p,f);
    case UOS_SYS_SYNC:return vfs_data_sync()==FS_OK ? 0 : UOS_ERROR;
    case UOS_SYS_ARGC:return p->args.count;
    case UOS_SYS_ARG:if (a>=p->args.count) { return UOS_ERROR; }return string_out(p,b,c,p->args.bytes+p->args.offset[a]);
    case UOS_SYS_GETCWD:return string_out(p,a,b,p->cwd);
    case UOS_SYS_GETPID:return (int64_t)(p->owner&~(1ULL<<63))+1;
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
static int64_t spawn(struct process *p,uint64_t address,uint64_t seconds,bool redirected,uint64_t streams)
{
    /* One generation of children; one outstanding child per root process.
     * No inherited handles. IDs never repeat when a slot is reused. */
    if (p->parent!=PROCESS_LIMIT || p->child!=PROCESS_LIMIT || !seconds || seconds>30) { return UOS_ERROR; }
    if (!user_range(p,address,sizeof(struct app_args),false)) { return UOS_EFAULT; }
    struct app_args args;user_copy(p,address,&args,sizeof(args),false);if (!args_valid(&args)) { return UOS_ERROR; }
    struct uos_stdio io={0};char output[RAMFS_PATH_MAX];bool dir;
    if (redirected) {
        if (!user_range(p,streams,sizeof(io),false)) { return UOS_EFAULT; }
        user_copy(p,streams,&io,sizeof(io),false);
        unsigned in=0,out=0;while (in<sizeof(io.input) && io.input[in]) { ++in; }while (out<sizeof(io.output) && io.output[out]) { ++out; }
        if (in==sizeof(io.input) || out==sizeof(io.output)) { return UOS_ERROR; }
        /* No replacement or truncation. Kernel work is serialized, so this
         * absence check and the eventual CREATE cannot race another process. */
        if (out && (io.output[out-1]=='/' || vfs_resolve(p->cwd,io.output,output,&dir)!=FS_NOT_FOUND ||
            vfs_prepare_write(p->cwd,io.output,output)!=FS_OK)) { return UOS_ERROR; }
    }
    unsigned slot=0;while (slot<PROCESS_LIMIT && tasks[slot].status!=UNUSED) { ++slot; }if (slot==PROCESS_LIMIT) { return UOS_ERROR; }
    struct process *child=&tasks[slot];zero(child,sizeof(*child));
    if (!load(child,args.bytes,p->cwd,&args) ||
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
        if (f->x[8]==UOS_SYS_SPAWN || f->x[8]==UOS_SYS_SPAWN_IO) { f->x[0]=(uint64_t)spawn(p,f->x[0],f->x[1],f->x[8]==UOS_SYS_SPAWN_IO,f->x[2]);return true; }
        if (f->x[8]==UOS_SYS_WAIT) {
            if (p->child>=total || f->x[0]!=(tasks[p->child].owner&~(1ULL<<63))+1) { f->x[0]=(uint64_t)UOS_ERROR;return true; }
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
            if (p->stdin_handle || !interactive || (current!=0 && (p->parent!=0 || tasks[0].child!=current || !live(&tasks[0])))) { f->x[0]=(uint64_t)UOS_ENOTTY;return true; }
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
    console_write(" terminal=");console_write(interactive ? "yes" : "no");console_write(" budget_seconds=");console_u64(budget_seconds);
    console_write(" children_started=");console_u64(children_spawned);console_write(" children_reaped=");console_u64(children_reaped);
    console_write(" last_child_code=");console_u64((unsigned)child_last_code);
    console_write(" cleanup=");console_write(last_clean ? "restored" : "not-run/failed");console_write("\n");
    for (unsigned i=0;i<total;++i) {
        const struct process *p=&tasks[i];if (p->status==UNUSED) { continue; }console_write("process=");console_u64(i);console_write(" path=");console_write(p->path);
        console_write(" state=");console_write(p->status==EXITED ? "exited" : p->status==FAULTED ? "faulted" : p->status==TIMED_OUT ? "timeout" : p->status==CANCELLED ? "cancelled" : "not-run");
        console_write(" code=");console_u64((unsigned)p->code);console_write(" syscalls=");console_u64(p->calls);console_write(" dispatches=");console_u64(p->dispatches);
        console_write(" sleeps=");console_u64(p->sleeps);console_write(" wakes=");console_u64(p->wakes);
        console_write(" input_lines=");console_u64(p->lines);
        if (p->esr) { console_write(" ESR=");console_hex(p->esr);console_write(" FAR=");console_hex(p->far); }
        console_write("\n");
    }
}
static int run(const char *const *paths,unsigned count,const struct app_args *args,unsigned terminal_seconds)
{
    if (!paths || !count || count>PROCESS_LIMIT || terminal_seconds>300 || (terminal_seconds && count!=1) || running || !mmu_enabled() || !mmu_dma_compatible() || !irq_timer_running() || epoch>=(1ULL<<60)-2) {
        console_write("App run refused: require mmuinit, irqinit, timerstart and 1..3 valid paths.\n");return 2;
    }
    uint64_t flags=irq_save();if (flags&0x80) { irq_restore(flags);console_write("App run refused: CPU IRQs must be unmasked.\n");return 2; }
    struct page_stats before=pages_stats();struct heap_stats heap=heap_get_stats();unsigned handles=kfile_count();
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(kernel_root));
    zero(tasks,sizeof(tasks));++epoch;total=count;current=0;switches=preemptions=yields=idles=0;last_clean=false;bool loaded=true,clean=true;
    children_spawned=children_reaped=0;child_last_code=0;child_cleanup_failed=false;
    interactive=terminal_seconds!=0;budget_seconds=interactive ? terminal_seconds : 2;
    input_count=input_head=0;input_dropping=false;zero(input_queue,sizeof(input_queue));zero(&input_line,sizeof(input_line));
    for (unsigned i=0;i<count;++i) { if (!paths[i] || !load(&tasks[i],paths[i],filesystem_cwd(),args)) { loaded=false;break; } }
    zero(image,sizeof(image));
    if (loaded) {
        if (interactive) { console_write("Terminal assigned to EL0; Ctrl+C cancels; session_seconds=");console_u64(budget_seconds);console_write("\n"); }
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
    zero(input_queue,sizeof(input_queue));zero(&input_line,sizeof(input_line));input_count=input_head=0;input_dropping=false;
    bool success=loaded;
    for (unsigned i=0;i<total;++i) { if (tasks[i].status!=UNUSED && (tasks[i].status!=EXITED || tasks[i].code)) { success=false; }if (!release(&tasks[i])) { clean=false; } }
    struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    /* RAM files created by applications legitimately retain heap storage.
     * Account for that ownership; private process pages must all be freed. */
    last_clean=clean && !child_cleanup_failed && before.allocated_pages+final.pages==after.allocated_pages+heap.pages && before.free_pages+heap.pages==after.free_pages+final.pages &&
        handles==kfile_count() && heap_check() && mmu_dma_compatible();
    irq_restore(flags);process_info();
    console_write(last_clean ? "App resources restored; monitor ready.\n" : "FAIL: application resource restoration.\n");
    return !loaded ? 2 : success && last_clean ? 0 : 1;
}
int process_run(const char *const *paths,unsigned count) { return run(paths,count,NULL,0); }
static int exec_args(const struct app_args *args,unsigned seconds)
{
    if (!args_valid(args)) { return 2; }const char *paths[]={args->bytes};return run(paths,1,args,seconds);
}
int process_exec(const struct app_args *args) { return exec_args(args,0); }
int process_terminal(const struct app_args *args,unsigned seconds) { return seconds && seconds<=300 ? exec_args(args,seconds) : 2; }
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
