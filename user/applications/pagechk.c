#include "user.h"

enum { PAGE=4096, RW=UOS_READ|UOS_WRITE };
static uint64_t thread_pointer(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}

static bool resident(unsigned count,unsigned rights)
{
    struct uos_vm_info info;
    return !uos_vm_info(0,&info) && info.count==1 && info.pages==3 &&
        info.entries[0].flags==UOS_VM_DEMAND && info.entries[0].resident_pages==count &&
        info.entries[0].rights==rights;
}

static int write_denied(void)
{
    if (thread_pointer()) { return 4; }
    uint64_t token;
    int64_t base=uos_vm_reserve(1,RW,&token);
    if (base<=0 || uos_vm_protect(token,UOS_READ)) { return 2; }
    *(volatile char *)(uintptr_t)base='x';
    return 3;
}

static bool run_checks(void)
{
    uint64_t token=0,other=0;
    int64_t base=uos_vm_reserve(3,RW,&token);
    if (base<=0 || !resident(0,RW) || uos_vm_map(token)!=UOS_ENOENT) { return false; }
    volatile unsigned char *page=(void *)(uintptr_t)base;
    if (page[0] || !resident(1,RW)) { return false; }
    page[0]=0x51;
    int64_t h=uos_open("/boot/LARGE.BIN",UOS_READ);
    if (h<=0 || uos_pread((uint64_t)h,(void *)(uintptr_t)(base+3*PAGE-2),4,0)!=UOS_EFAULT ||
        !resident(1,RW) || uos_pread((uint64_t)h,(void *)(uintptr_t)(base+2*PAGE-2),4,0)!=4 ||
        uos_close((uint64_t)h) || !resident(3,RW)) { return false; }
    if (page[PAGE] || page[2*PAGE-2]!=7 || page[2*PAGE-1]!=36 || page[2*PAGE]!=65 || page[2*PAGE+1]!=94) { return false; }
    if (uos_vm_protect(token,UOS_READ) || !resident(3,UOS_READ) || page[0]!=0x51 ||
        uos_vm_protect(token,RW) || uos_vm_unmap(token) || uos_write((const void *)(uintptr_t)base,1)!=UOS_EFAULT) { return false; }
    base=uos_vm_reserve(32,RW,&token);
    if (base<=0 || uos_vm_reserve(17,RW,&other)!=UOS_ENOSPC || other || uos_vm_unmap(token)) { return false; }

    struct app_args args={.count=2,.offset={0,sizeof("/boot/PAGECHK.ELF")},
        .used=sizeof("/boot/PAGECHK.ELF\0fault"),.bytes="/boot/PAGECHK.ELF\0fault"};
    int64_t child=uos_spawn(&args,1);
    struct uos_fault_info fault;
    struct uos_child_info info;
    if (child<=0) { return false; }
    do {
        if (uos_child_poll((uint64_t)child,&info)) { return false; }
        if (!info.finished) { (void)uos_yield(); }
    } while (!info.finished);
    return thread_pointer()==0x12345678 && !uos_child_fault((uint64_t)child,&fault) && fault.code==125 && (fault.flags&UOS_FAULT_FAR_VALID) &&
        ((fault.esr>>26)&63)==0x24 && (fault.esr&63)==7 && uos_wait((uint64_t)child)==125;
}

int app_main(void)
{
    if (uos_argc()==2) { return write_denied(); }
    if (thread_pointer()) { return 2; }
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(0x12345678ULL) : "memory");
    if (!run_checks()) { uos_puts("FAIL: demand paging checks.\n");return 1; }
    uos_puts("PASS: demand-zero pages, syscall fault-in, quota, protection and cleanup.\n");
    return 0;
}
