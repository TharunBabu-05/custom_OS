#include "exception.h"
#include "mmu.h"
#include "irq.h"
#include "console.h"
static bool dispatching,testing,observed;
static uint64_t sync_count,irq_count;
extern unsigned char exception_test_trap[],exception_test_resume[];
extern uint64_t exception_test_registers(void);
const char *exception_class(uint64_t esr)
{
    switch ((esr>>26)&63) {
    case 0:return "undefined/unknown instruction";case 0x15:return "AArch64 SVC";
    case 0x20:case 0x21:return "instruction abort";case 0x24:case 0x25:return "data abort";
    case 0x22:return "PC alignment";case 0x26:return "SP alignment";case 0x3c:return "AArch64 BRK";default:return "unhandled exception class";
    }
}
void exception_dispatch(struct exception_frame *f)
{
    if (!f || dispatching) { if (f) { exception_frame_fatal(f); }for (;;) { __asm__ volatile("wfi"); } }
    dispatching=true;
    if (f->vector==5) { ++irq_count;if (!irq_dispatch()) { f->spsr|=0x80; }dispatching=false;return; }
    if (f->vector==4) {
        ++sync_count;
        if (testing && f->elr==(uintptr_t)exception_test_trap && f->esr==0xf2000043ULL) { observed=true;testing=false;f->elr=(uintptr_t)exception_test_resume;dispatching=false;return; }
        uintptr_t resume=mmu_handle_probe_abort(f->esr,f->elr,f->far);
        if (resume) { f->elr=resume;dispatching=false;return; }
    }
    irq_fatal_stop();exception_frame_fatal(f);
}
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
bool exception_command(const char *line)
{
    if (same(line,"excinfo")) {
        uint64_t flags=irq_save(),sync=sync_count,irqs=irq_count;irq_restore(flags);
        console_write("Exception frames: x0..x30, ELR/SPSR/ESR/FAR/vector; 288 bytes, 16-byte aligned.\nsynchronous=");console_u64(sync);console_write(" IRQ=");console_u64(irqs);console_write("\nOnly armed BRK/MMU tests resume. Unexpected exceptions halt; no EL0/process recovery yet.\n");return true;
    }
    if (same(line,"exctest")) {
        uint64_t flags=irq_save();testing=true;observed=false;uint64_t bad=exception_test_registers();bool good=observed && !testing && !bad;testing=false;irq_restore(flags);
        console_write(good ? "PASS: synchronous exception returned with all 31 GPRs, SP and NZCV preserved.\n" : "FAIL: exception register-return test.\n");return true;
    }return false;
}
