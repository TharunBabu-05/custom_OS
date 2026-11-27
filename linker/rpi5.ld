ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    /* BOOT_TEST_BASE is defined only by the QEMU handoff-test build. */
    . = DEFINED(BOOT_TEST_BASE) ? BOOT_TEST_BASE : 0x80000;
    __kernel_start = .;
    __boot_dtb_start = __kernel_start - 0x80000 + 0x02000000;
    __text_start = .;
    .text : {
        KEEP(*(.text.boot))
        /* Fit CPU entry and lifecycle code into exception-vector alignment padding. */
        *(.text.cpu_lifecycle)
        *(.text .text.*)
        . = ALIGN(4096);
    } :code
    __text_end = .;
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4096); } :code
    __rodata_end = .;
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    __image_file_end = .;
    .bss (NOLOAD) : {
        . = ALIGN(8); __bss_start = .;
        /* Place the page-aligned tables, stacks and DMA buffers first. */
        *(SORT_BY_ALIGNMENT(.bss*)) *(COMMON)
        /* Exercise a footprint beyond the former 1.5 MiB cap in QEMU. */
        . = MAX(., DEFINED(BOOT_TEST_BASE) ? 0x200000 : 0);
        . = ALIGN(8); __bss_end = .;
    } :data
    .stack_guard (NOLOAD) : {
        . = ALIGN(4096); __stack_guard = .; . += 4096;
    } :data
    .stack (NOLOAD) : {
        . = ALIGN(4096); __stack_bottom = .;
        . += 0x10000; __stack_top = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(__kernel_end <= __boot_dtb_start, "Image overlaps reserved DTB region")
}
