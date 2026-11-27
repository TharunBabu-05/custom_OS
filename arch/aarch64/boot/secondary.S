#include "arm_platform.h"
#include "smp.h"
#include "el2.inc"

.section .text.cpu_lifecycle, "ax"
.global smp_secondary_entry
.type smp_secondary_entry, %function
smp_secondary_entry:
    msr daifset, #15
    mrs x1, CurrentEL
    cmp x1, #8
    b.eq .Lcheck_el2
    cmp x1, #4
    b.ne .Lpark
    mrs x2, sctlr_el1
    b .Lcheck_context
.Lcheck_el2:
    mrs x2, sctlr_el2
.Lcheck_context:
    tbnz x2, #0, .Lpark        /* PSCI entry uses physical addresses. */
    tbnz x2, #2, .Lpark
    adrp x2, smp_workers
    add x2, x2, :lo12:smp_workers
    sub x2, x0, x2
    cmp x2, #SMP_WORKER_LIMIT*SMP_WORKER_SIZE
    b.hs .Lpark
    tst x2, #SMP_WORKER_SIZE-1
    b.ne .Lpark
    ldr x3, [x0]                /* Published stack top. */
    dmb sy
    cbz x3, .Lpark
    tst x3, #4095
    b.ne .Lpark
    mrs x4, mpidr_el1
    ldr x5, =ARM_AFFINITY_MASK
    and x5, x4, x5
    ldr x2, [x0, #8]            /* Device-tree affinity. */
    cmp x2, x5
    b.ne .Lpark
    cmp x1, #8
    b.ne .Lenter_el1
    prepare_el1 x2
    msr vmpidr_el2, x4
    mrs x2, midr_el1
    msr vpidr_el2, x2
    msr sp_el1, x3
    adr x2, .Lenter_el1
    msr elr_el2, x2
    mov x2, #0x3c5              /* EL1h, exceptions masked. */
    msr spsr_el2, x2
    isb
    eret
.Lenter_el1:
    msr spsel, #1
    mov sp, x3
    msr tpidr_el1, x0
    adr x2, exception_vectors
    msr vbar_el1, x2
    ldr x2, =0x30d00800
    msr sctlr_el1, x2
    isb
    ic iallu
    dsb sy
    isb
    msr cpacr_el1, xzr
    msr tpidrro_el0, xzr
    msr cntp_ctl_el0, xzr
    msr cntv_ctl_el0, xzr
    lsr x1, x1, #2
    bl smp_worker_main
.Lpark:
    wfi
    b .Lpark
.size smp_secondary_entry, . - smp_secondary_entry

.global smp_test_break
.type smp_test_break, %function
smp_test_break:
    brk #0x41
    ret
.size smp_test_break, . - smp_test_break
.section .note.GNU-stack, "", %progbits
