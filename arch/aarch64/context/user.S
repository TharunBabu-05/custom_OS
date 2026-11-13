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
process_arch_enter:               /* x0=frame, x1=user SP; IRQs masked */
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x30, x0                 /* complete saved integer frame */
    msr sp_el0, x1
    ldp x2, x3, [x30, #248]
    msr elr_el1, x2
    msr spsr_el1, x3
    ldp x0, x1, [x30, #0]
    ldp x2, x3, [x30, #16]
    ldp x4, x5, [x30, #32]
    ldp x6, x7, [x30, #48]
    ldp x8, x9, [x30, #64]
    ldp x10, x11, [x30, #80]
    ldp x12, x13, [x30, #96]
    ldp x14, x15, [x30, #112]
    ldp x16, x17, [x30, #128]
    ldp x18, x19, [x30, #144]
    ldp x20, x21, [x30, #160]
    ldp x22, x23, [x30, #176]
    ldp x24, x25, [x30, #192]
    ldp x26, x27, [x30, #208]
    ldp x28, x29, [x30, #224]
    ldr x30, [x30, #240]
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
