#include "board.h"
#include "console.h"
bool board_init(struct board_info *info, uintptr_t dtb)
{
    /* ELF startup supplies RAM start; Image startup preserves firmware x0. */
    if (!fdt_read_board((const void *)dtb, 0x200000, info)) { return false; }
    if (info->ram_base != 0x40000000UL || info->ram_size < 0x1000000UL) { return false; }
    info->name="qemu_virt";
    info->ram_known=true;
    return uart_init(info->uart_base, info->uart_clock, 115200);
}
