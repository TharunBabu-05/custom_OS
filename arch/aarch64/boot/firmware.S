/* AArch64 Image ABI used by Pi 5 firmware. See docs/rpi5-boot.md.
 * MMU and data cache must be off on entry; EL1 or EL2, little endian.
 * x0 (DTB) is preserved. EL3 is deliberately unsupported. */
.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    b firmware_entry
    nop
    .quad 0x80000
    .quad __kernel_end - _start
    .quad 8                       /* LE; unspecified page size; physical placement flag */
    .quad 0
    .quad 0
    .quad 0
    .long 0x644d5241              /* ARM64 Image magic */
    .long 0
.global firmware_entry
firmware_entry:
    msr daifset, #15
    mov x20, x0
    mrs x21, CurrentEL
    lsr x21, x21, #2
    mrs x1, mpidr_el1
    and x1, x1, #0xffffff
    cbnz x1, .Lpark
    cmp x21, #2
    b.eq .Lel2
    cmp x21, #1
    b.ne .Lpark
    mrs x1, sctlr_el1
    tbnz x1, #0, .Lpark           /* No unsafe blind MMU/cache disable. */
    tbnz x1, #2, .Lpark
    b .Lel1
.Lel2:
    mrs x1, sctlr_el2
    tbnz x1, #0, .Lpark
    tbnz x1, #2, .Lpark
    mov x1, #0x80000000           /* HCR_EL2.RW: EL1 uses AArch64; no stage 2. */
    msr hcr_el2, x1
    isb
    mov x1, #3                   /* Allow EL1 physical counter/timer access. */
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    msr vttbr_el2, xzr
    msr hstr_el2, xzr
    mov x1, #0x33ff              /* CPTR_EL2 RES1, no FP/sysreg traps. */
    msr cptr_el2, x1
    ldr x1, =0x30d00800          /* SCTLR_EL1 RES1; M/C/I clear. */
    msr sctlr_el1, x1
    ldr x1, =__stack_top
    msr sp_el1, x1
    adr x1, .Lel1
    msr elr_el2, x1
    mov x1, #0x3c5              /* DAIF masked, EL1h. */
    msr spsr_el2, x1
    isb
    eret
.Lel1:
    msr spsel, #1
    ldr x1, =0x30d00800
    msr sctlr_el1, x1
    isb
    ic iallu
    dsb sy
    isb
    ldr x1, =__stack_top
    mov sp, x1
    msr cpacr_el1, xzr
    ldr x1, =__bss_start
    ldr x2, =__bss_end
.Lzero:
    cmp x1, x2
    b.hs .Lready
    str xzr, [x1], #8
    b .Lzero
.Lready:
    ldr x1, =exception_vectors
    msr vbar_el1, x1
    isb
    mov x0, x20
    mov x1, x21
    bl kernel_main
.Lpark:
    wfi
    b .Lpark
.size _start, . - _start
.section .note.GNU-stack, "", %progbits
