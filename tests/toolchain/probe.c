/* Compile/link inspection only. This is not a kernel or a bootable program. */
#if !defined(__aarch64__)
#error "This probe requires an AArch64 compiler"
#endif
#if __STDC_HOSTED__ != 0
#error "This probe must be compiled freestanding"
#endif
_Static_assert(sizeof(void *) == 8, "64-bit pointers required");
_Static_assert(sizeof(unsigned long) == 8, "LP64 ABI required");

volatile unsigned long probe_result;
extern unsigned long probe_current_el(void);
void probe_entry(void);

void probe_entry(void)
{
    probe_result = 0x1234UL + probe_current_el();
    for (;;) {
        __asm__ volatile ("nop");
    }
}
