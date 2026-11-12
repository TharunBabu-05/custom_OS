/* Public callee-saved patterns survive explicit yield calls; AAPCS64 permits
 * caller-saved registers and NZCV to change across these procedure calls. */
.section .text, "ax"
.global task_context_selftest
.type task_context_selftest, %function
task_context_selftest:
    cbz w0, .Lempty
    sub sp, sp, #128
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    str x0, [sp, #96]
    mov x0, sp
    str x0, [sp, #104]
.Lround:
    mov x19, #2323
    mov x20, #2324
    mov x21, #2325
    mov x22, #2326
    mov x23, #2327
    mov x24, #2328
    mov x25, #2329
    mov x26, #2330
    mov x27, #2331
    mov x28, #2332
    mov x29, #2333
    bl task_yield
    cbz w0, .Lbad
    cmp x19, #2323
    b.ne .Lbad
    cmp x20, #2324
    b.ne .Lbad
    cmp x21, #2325
    b.ne .Lbad
    cmp x22, #2326
    b.ne .Lbad
    cmp x23, #2327
    b.ne .Lbad
    cmp x24, #2328
    b.ne .Lbad
    cmp x25, #2329
    b.ne .Lbad
    cmp x26, #2330
    b.ne .Lbad
    cmp x27, #2331
    b.ne .Lbad
    cmp x28, #2332
    b.ne .Lbad
    cmp x29, #2333
    b.ne .Lbad
    mov x0, sp
    ldr x1, [sp, #104]
    cmp x0, x1
    b.ne .Lbad
    ldr x0, [sp, #96]
    subs x0, x0, #1
    str x0, [sp, #96]
    b.ne .Lround
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
.Lempty:
    mov x0, #0
    ret
.size task_context_selftest, . - task_context_selftest
.section .note.GNU-stack, "", %progbits

