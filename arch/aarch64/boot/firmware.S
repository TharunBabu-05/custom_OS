#include "el2.inc"

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
    prepare_el1 x1
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
    /* Preserve the validated firmware blob before clearing BSS, including
     * legacy boots whose original DTB lay inside the runtime span. */
    cbz x20, .Lclear
    tst x20, #7
    b.ne .Lbad_dtb
    ldr x6, =__image_file_end
    adr x7, _start
    cmp x20, x7
    b.lo .Lread_dtb
    cmp x20, x6
    b.lo .Lbad_dtb
.Lread_dtb:
    ldr w1, [x20]
    ldr w2, =0xedfe0dd0           /* FDT magic stored big-endian. */
    cmp w1, w2
    b.ne .Lbad_dtb
    ldr w1, [x20, #4]
    rev w1, w1
    cmp w1, #40
    b.lo .Lbad_dtb
    cmp w1, #0x200000
    b.hi .Lbad_dtb
    adds x2, x20, x1
    b.cs .Lbad_dtb
    ldr x3, =__boot_dtb_start
    cmp x20, x3
    b.eq .Lclear
    mov x4, x20
    mov x20, x3
    cmp x3, x4
    b.lo .Lcopy_forward
    add x3, x3, x1
.Lcopy_back:
    ldrb w5, [x2, #-1]!
    strb w5, [x3, #-1]!
    subs x1, x1, #1
    b.ne .Lcopy_back
    b .Lclear
.Lcopy_forward:
    ldrb w5, [x4], #1
    strb w5, [x3], #1
    subs x1, x1, #1
    b.ne .Lcopy_forward
    b .Lclear
.Lbad_dtb:
    mov x20, xzr
.Lclear:
    msr cpacr_el1, xzr
    msr tpidrro_el0, xzr
    ldr x1, =__bss_start
    ldr x2, =__bss_end
.Lzero:
    cmp x1, x2
    b.hs .Lready
    str xzr, [x1], #8
    b .Lzero
.Lready:
    msr tpidr_el1, xzr
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
