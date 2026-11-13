OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS
{
    image PT_LOAD FILEHDR PHDRS FLAGS(5);
}
SECTIONS
{
    /* The single read/execute page includes ELF headers at file offset 0.
     * Stack storage is supplied separately by the kernel's EL0 mapping. */
    . = 0x2000000000 + SIZEOF_HEADERS;
    .text : ALIGN(16) { KEEP(*(.text.start)) *(.text .text.*) } :image
    .rodata : ALIGN(16) { *(.rodata .rodata.*) } :image
    .data : ALIGN(16) { *(.data .data.*) } :image
    .bss (NOLOAD) : ALIGN(16) { *(.bss .bss.* COMMON) } :image
    ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "sample applications must use stack locals, no writable globals")
    ASSERT(. - 0x2000000000 <= 4096, "application exceeds one image page")
    /DISCARD/ : { *(.comment .note.* .eh_frame* .ARM.exidx* .ARM.extab*) }
}
