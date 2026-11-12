/* Common non-nesting EL1 exception/IRQ frame; offsets match exception.h. */
.section .text.vectors, "ax"
.balign 2048
.global exception_vectors
exception_vectors:
.macro slot number
    .balign 128
    sub sp, sp, #288
    stp x0, x1, [sp, #0]
    mov x0, #\number
    b .Lentry
.endm
    slot 0
    slot 1
    slot 2
    slot 3
    slot 4
    slot 5
    slot 6
    slot 7
    slot 8
    slot 9
    slot 10
    slot 11
    slot 12
    slot 13
    slot 14
    slot 15
.balign 128
.Lentry:
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    str x0, [sp, #280]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #248]
    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [sp, #264]
    mov x0, sp
    bl exception_dispatch
    ldp x0, x1, [sp, #248]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #288
    eret
.section .note.GNU-stack, "", %progbits