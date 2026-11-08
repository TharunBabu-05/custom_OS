/* QEMU virt ELF boot contract: one CPU, EL1, MMU and caches initially off.
 * This entry is not yet a Raspberry Pi firmware entry implementation. */
.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    msr daifset, #15
    mrs x1, mpidr_el1
    and x1, x1, #0xffffff
    cbnz x1, .Lpark
    mrs x1, CurrentEL
    cmp x1, #4
    b.ne .Lpark
    msr spsel, #1
    ldr x1, =__stack_top
    mov sp, x1
    ldr x1, =__bss_start
    ldr x2, =__bss_end
.Lzero:
    cmp x1, x2
    b.hs .Lbss_done
    str xzr, [x1], #8
    b .Lzero
.Lbss_done:
    ldr x1, =exception_vectors
    msr vbar_el1, x1
    isb
    ldr x0, =0x40000000
    mov x1, #1
    bl kernel_main
.Lpark:
    wfi
    b .Lpark
.size _start, . - _start
.section .note.GNU-stack, "", %progbits
