ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    /* BOOT_TEST_BASE is defined only by the QEMU handoff-test build. */
    . = DEFINED(BOOT_TEST_BASE) ? BOOT_TEST_BASE : 0x80000;
    __kernel_start = .;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : {
        . = ALIGN(8); __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(8); __bss_end = .;
    } :data
    .stack (NOLOAD) : {
        . = ALIGN(16); __stack_bottom = .;
        . += 0x10000; __stack_top = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(__kernel_end - __kernel_start < 0x180000, "Image overlaps reserved DTB region")
}
