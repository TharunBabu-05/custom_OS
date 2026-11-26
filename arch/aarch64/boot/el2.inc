.macro prepare_el1 scratch
    mov \scratch, #0x80000000    /* HCR_EL2.RW: AArch64 EL1, no stage 2. */
    msr hcr_el2, \scratch
    isb
    mov \scratch, #3
    msr cnthctl_el2, \scratch
    msr cntvoff_el2, xzr
    msr vttbr_el2, xzr
    msr hstr_el2, xzr
    mov \scratch, #0x33ff
    msr cptr_el2, \scratch
    ldr \scratch, =0x30d00800    /* SCTLR_EL1 RES1 with M/C/I clear. */
    msr sctlr_el1, \scratch
.endm
