ENTRY(_start)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    /* Leave 2 MiB at RAM start for the QEMU-provided DTB. */
    . = 0x40200000;
    __kernel_start = .;
    __text_start = .;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) . = ALIGN(4096); } :code
    __text_end = .;
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4096); } :code
    __rodata_end = .;
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } :data
    .stack_guard (NOLOAD) : {
        . = ALIGN(4096); __stack_guard = .; . += 4096;
    } :data
    .stack (NOLOAD) : {
        . = ALIGN(4096);
        __stack_bottom = .;
        . += 0x10000;
        __stack_top = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(__kernel_end < 0x41000000, "Early image exceeds its 16 MiB RAM contract")
}
