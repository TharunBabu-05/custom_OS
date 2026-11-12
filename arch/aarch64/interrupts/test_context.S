/* Public register pattern; exact BRK #0x43 recovery is armed only by exctest. */
.section .text, "ax"
.global exception_test_registers, exception_test_trap, exception_test_resume
.type exception_test_registers, %function
exception_test_registers:
    sub sp, sp, #128
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x0, sp
    str x0, [sp, #104]
    mov x0, #0xa0000000
    msr nzcv, x0
    mov x0, #256
    mov x1, #257
    mov x2, #258
    mov x3, #259
    mov x4, #260
    mov x5, #261
    mov x6, #262
    mov x7, #263
    mov x8, #264
    mov x9, #265
    mov x10, #266
    mov x11, #267
    mov x12, #268
    mov x13, #269
    mov x14, #270
    mov x15, #271
    mov x16, #272
    mov x17, #273
    mov x18, #274
    mov x19, #275
    mov x20, #276
    mov x21, #277
    mov x22, #278
    mov x23, #279
    mov x24, #280
    mov x25, #281
    mov x26, #282
    mov x27, #283
    mov x28, #284
    mov x29, #285
    mov x30, #286
exception_test_trap:
    brk #0x43
exception_test_resume:
    str x0, [sp, #120]
    mrs x0, nzcv
    str x0, [sp, #112]
    ldr x0, [sp, #120]
    cmp x0, #256
    b.ne .Lbad
    cmp x1, #257
    b.ne .Lbad
    cmp x2, #258
    b.ne .Lbad
    cmp x3, #259
    b.ne .Lbad
    cmp x4, #260
    b.ne .Lbad
    cmp x5, #261
    b.ne .Lbad
    cmp x6, #262
    b.ne .Lbad
    cmp x7, #263
    b.ne .Lbad
    cmp x8, #264
    b.ne .Lbad
    cmp x9, #265
    b.ne .Lbad
    cmp x10, #266
    b.ne .Lbad
    cmp x11, #267
    b.ne .Lbad
    cmp x12, #268
    b.ne .Lbad
    cmp x13, #269
    b.ne .Lbad
    cmp x14, #270
    b.ne .Lbad
    cmp x15, #271
    b.ne .Lbad
    cmp x16, #272
    b.ne .Lbad
    cmp x17, #273
    b.ne .Lbad
    cmp x18, #274
    b.ne .Lbad
    cmp x19, #275
    b.ne .Lbad
    cmp x20, #276
    b.ne .Lbad
    cmp x21, #277
    b.ne .Lbad
    cmp x22, #278
    b.ne .Lbad
    cmp x23, #279
    b.ne .Lbad
    cmp x24, #280
    b.ne .Lbad
    cmp x25, #281
    b.ne .Lbad
    cmp x26, #282
    b.ne .Lbad
    cmp x27, #283
    b.ne .Lbad
    cmp x28, #284
    b.ne .Lbad
    cmp x29, #285
    b.ne .Lbad
    cmp x30, #286
    b.ne .Lbad
    ldr x0, [sp, #112]
    mov x1, #0xa0000000
    cmp x0, x1
    b.ne .Lbad
    mov x0, sp
    ldr x1, [sp, #104]
    cmp x0, x1
    b.ne .Lbad
    mov x0, #0
    b .Lreturn
.Lbad:
    mov x0, #1
.Lreturn:
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #128
    ret
.size exception_test_registers, . - exception_test_registers
.section .note.GNU-stack, "", %progbits

