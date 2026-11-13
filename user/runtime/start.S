.section .text.start,"ax"
.balign 16
.global _start
.type _start,%function
_start:
    mov x29, xzr
    mov x30, xzr
    bl app_main
    mov x8, #0
    svc #0
1:  b 1b
.size _start, .-_start
.section .note.GNU-stack,"",%progbits
