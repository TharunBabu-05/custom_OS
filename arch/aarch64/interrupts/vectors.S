/* Fatal diagnostics only: IRQs remain masked; no recoverable context frame yet. */
.section .text.vectors, "ax"
.balign 2048
.global exception_vectors
exception_vectors:
.macro slot number
    .balign 128
    mov x0, #\number
    b .Lfatal
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
.Lfatal:
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    b exception_fatal
.section .note.GNU-stack, "", %progbits
