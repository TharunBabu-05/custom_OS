#include "user.h"
static bool has_caller(const struct uos_process_entry *entries,unsigned count,uint64_t pid)
{
    for (unsigned i=0;i<count;++i) {
        if (entries[i].pid==pid && entries[i].flags==UOS_PROCESS_SELF) { return true; }
    }
    return false;
}
int app_main(void)
{
    struct app_args a={0};const char *path="/boot/HELLO.ELF";a.count=1;
    do { a.bytes[a.used++]=*path; }while (*path++);
    if (uos_spawn(&a,1)!=UOS_ERROR || uos_spawn_ex(&a,1,0,0)!=UOS_ERROR || uos_pipeline(&a,&a,1,0)!=UOS_ERROR) { return 1; }
    struct uos_process_table v1;
    struct uos_process_table_v2 v2;
    struct uos_process_table_v3 v3;
    uint64_t pid=(uint64_t)uos_getpid();
    if (uos_processes(&v1) || v1.version!=UOS_PROCESS_VERSION || v1.count!=UOS_PROCESS_MAX ||
        v1.caller_pid!=pid || !has_caller(v1.entries,v1.count,pid) ||
        uos_processes_v2(&v2) || v2.version!=UOS_PROCESS_VERSION_V2 || v2.count!=UOS_PROCESS_MAX_V2 ||
        v2.caller_pid!=pid || !has_caller(v2.entries,v2.count,pid)) { return 2; }
    if (uos_processes_v3(0)!=UOS_EFAULT || uos_processes_v3(&v3) || v3.version!=UOS_PROCESS_VERSION_V3 ||
        v3.count<UOS_PIPE_STAGES+1 || v3.count>UOS_PROCESS_MAX_V3 || v3.caller_pid!=pid) { return 3; }
    unsigned apps=0;
    bool self=false;
    for (unsigned i=0;i<v3.count;++i) {
        const struct uos_process_entry_v3 *row=&v3.entries[i];
        if (!(row->process.flags&UOS_PROCESS_SERVICE)) { ++apps; }
        if (!row->process.pid || !row->quantum_ticks || row->reserved || row->process.parent_pid) { return 4; }
        if (has_caller(&row->process,1,pid)) { self=true; }
    }
    if (!self || apps!=UOS_PIPE_STAGES+1) { return 5; }
    uos_puts("PASS: full process table refuses child creation.\n");
    uos_puts("PASS: eight foreground slots and compatible process snapshots.\n");return 0;
}
