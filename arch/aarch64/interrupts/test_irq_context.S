/* Asynchronous IRQ register sentinel.  The caller enables the periodic timer
 * and verifies delivered IRQs before/after this test.  Each wait preserves
 * NZCV and all registers except its two counter/deadline scratch registers;
 * the other pass checks those registers.  No WFI: a broken IRQ cannot leave
 * the monitor asleep.  A separate iteration limit also bounds a stuck clock.
 *
 * uint64_t irq_test_registers(uint64_t duration_ticks): 0 = pass, 1 = fail.
 * Each pass waits duration_ticks; accept 0 < duration_ticks < 2^63.
 */
.section .text, "ax"
.global irq_test_registers
.type irq_test_registers, %function

.macro irq_pattern
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
        mov x\reg, #(256 + \reg)
    .endr
.endm

.macro irq_check skip_a, skip_b
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
        .if \reg != \skip_a
            .if \reg != \skip_b
                cmp x\reg, #(256 + \reg)
                b.ne .Lirq_bad
            .endif
        .endif
    .endr
.endm

.macro irq_prepare_pass
    ldr x16, [sp, #96]
    mrs x17, cntpct_el0
    add x16, x16, x17
    str x16, [sp, #104]
    mov x16, #0x1000000
    str x16, [sp, #136]
    mov x16, #0xa0000000
    msr nzcv, x16
    irq_pattern
.endm

.macro irq_check_flags_sp
    ldr x0, [sp, #120]
    mov x1, #0xa0000000
    cmp x0, x1
    b.ne .Lirq_bad
    mov x0, sp
    ldr x1, [sp, #112]
    cmp x0, x1
    b.ne .Lirq_bad
.endm

irq_test_registers:
    cbz x0, .Lirq_bad_early
    tbnz x0, #63, .Lirq_bad_early
    mrs x1, daif
    tbnz x1, #7, .Lirq_bad_early
    sub sp, sp, #160
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    str x0, [sp, #96]
    mov x1, sp
    str x1, [sp, #112]

    irq_prepare_pass
.Lirq_wait_first:
    ldr x16, [sp, #136]
    sub x16, x16, #1
    str x16, [sp, #136]
    cbz x16, .Lirq_bad
    mrs x16, cntpct_el0
    ldr x17, [sp, #104]
    sub x16, x16, x17
    tbnz x16, #63, .Lirq_wait_first
    mrs x16, nzcv
    str x16, [sp, #120]
    irq_check 16, 17
    irq_check_flags_sp

    irq_prepare_pass
.Lirq_wait_second:
    ldr x14, [sp, #136]
    sub x14, x14, #1
    str x14, [sp, #136]
    cbz x14, .Lirq_bad
    mrs x14, cntpct_el0
    ldr x15, [sp, #104]
    sub x14, x14, x15
    tbnz x14, #63, .Lirq_wait_second
    mrs x14, nzcv
    str x14, [sp, #120]
    irq_check 14, 15
    irq_check_flags_sp
    mov x0, #0
    b .Lirq_return
.Lirq_bad:
    mov x0, #1
.Lirq_return:
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #160
    ret
.Lirq_bad_early:
    mov x0, #1
    ret
.size irq_test_registers, . - irq_test_registers
.section .note.GNU-stack, "", %progbits
