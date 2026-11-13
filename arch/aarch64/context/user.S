.section .text, "ax"
.global process_arch_enter,process_arch_return,process_arch_root
.type process_arch_root, %function
process_arch_root:
    dsb sy
    msr ttbr0_el1, x0
    isb
    tlbi vmalle1
    dsb sy
    isb
    ret
.size process_arch_root, .-process_arch_root
.type process_arch_enter, %function
process_arch_enter:               /* x0=entry, x1=user SP; IRQs masked */
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    msr elr_el1, x0
    msr sp_el0, x1
    mov x2, #0x340               /* EL0t, D/A/F masked, IRQ unmasked */
    msr spsr_el1, x2
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    eret
process_arch_return:
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #96
    ret
.size process_arch_enter, .-process_arch_enter
.section .note.GNU-stack, "", %progbits
