/* Exercises the assembler and C/assembly linkage. Never executed in Phase 1. */
.section .text.probe_current_el, "ax"
.balign 4
.global probe_current_el
.type probe_current_el, %function
probe_current_el:
    mrs x0, CurrentEL
    ret
.size probe_current_el, . - probe_current_el
.section .note.GNU-stack, "", %progbits
