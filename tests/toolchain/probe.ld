/* Arbitrary inspection address, NOT a board load address or boot contract. */
ENTRY(probe_entry)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x10000;
    .text : { *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
