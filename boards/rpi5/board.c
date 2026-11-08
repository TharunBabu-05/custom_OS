/* Firmware-initialized Pi 5 PL011 console. See docs/rpi5-header-uart.md for
 * the RP1 firmware contract and references. No PCIe reconfiguration here. */
#include "board.h"
#include "console.h"
bool board_init(struct board_info *info, uintptr_t dtb)
{
    (void)dtb;
    *info=(struct board_info){
#ifdef OUROS_RPI5_HEADER_UART
        .name="rpi5-header",
        /* Measured firmware mapping on the bring-up board (revision d04171):
         * RP1_UART 0000001c00030000. The Linux PCIe window at 0x1f... is
         * not necessarily the window used by firmware before an OS driver
         * reconfigures PCIe. Do not probe the other window speculatively. */
        .uart_base=0x1c00030000UL,
#else
        .name="rpi5",
        .uart_base=0x107d001000UL,
#endif
        .uart_clock=0,
        .ram_known=false
    };
    /* UART attachment remains separate from shared DTB memory discovery.
     * memory_boot_init parses RAM and reservations after the console is live. */
    return uart_attach_firmware(info->uart_base);
}
