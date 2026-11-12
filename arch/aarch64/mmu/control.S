/* Original single-core EL1 activation; table writes and translations are
 * synchronized before M=1. Identity mappings cover this code and its stack.
 * RAM/table walks remain non-cacheable; I/D caches stay disabled. */
.section .text, "ax"
.global mmu_arch_enable
.type mmu_arch_enable, %function
mmu_arch_enable:                 /* x0=root PA, x1=TCR, x2=MAIR */
    dsb sy
    msr mair_el1, x2
    msr tcr_el1, x1
    msr ttbr0_el1, x0
    msr ttbr1_el1, xzr
    isb
    tlbi vmalle1
    dsb sy
    isb
    mrs x3, sctlr_el1
    bic x3, x3, #4
    bic x3, x3, #0x1000
    orr x3, x3, #0x80000        /* WXN: writable mappings cannot execute. */
    orr x3, x3, #1
    msr sctlr_el1, x3
    isb
    ret
.size mmu_arch_enable, . - mmu_arch_enable

.global mmu_probe_read, mmu_probe_read_fault, mmu_probe_read_resume
.type mmu_probe_read, %function
mmu_probe_read:
mmu_probe_read_fault:
    ldr x2, [x0]
    mov w0, #0
    ret
mmu_probe_read_resume:
    mov w0, #1
    ret
.size mmu_probe_read, . - mmu_probe_read

.global mmu_probe_write, mmu_probe_write_fault, mmu_probe_write_resume
.type mmu_probe_write, %function
mmu_probe_write:
mmu_probe_write_fault:
    str xzr, [x0]
    mov w0, #0
    ret
mmu_probe_write_resume:
    mov w0, #1
    ret
.size mmu_probe_write, . - mmu_probe_write

.global mmu_probe_exec, mmu_probe_exec_resume
.type mmu_probe_exec, %function
mmu_probe_exec:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    blr x0                     /* Instruction fault's ELR is target, not BLR. */
    mov w0, #0
    ldp x29, x30, [sp], #16
    ret
mmu_probe_exec_resume:
    mov w0, #1
    ldp x29, x30, [sp], #16
    ret
.size mmu_probe_exec, . - mmu_probe_exec

.balign 8
.global mmu_text_marker
mmu_text_marker:
    nop
    nop
.section .rodata, "a"
.balign 8
.global mmu_rodata_marker
mmu_rodata_marker:
    .quad 0x6f75726f736d6d75
.section .note.GNU-stack, "", %progbits
