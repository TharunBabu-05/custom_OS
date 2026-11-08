/* Arm PL011 DDI0183: offsets and bits, 32-bit accesses.
 * The caller supplies the CPU physical base and UART input clock. */
#include "console.h"
enum { DR=0x00, FR=0x18, IBRD=0x24, FBRD=0x28, LCRH=0x2c,
       CR=0x30, IMSC=0x38, ICR=0x44 };
static uintptr_t uart_base;
static uint32_t read_reg(unsigned offset)
{
    uint32_t value = *(volatile uint32_t *)(uart_base + offset);
    __asm__ volatile ("dmb sy" ::: "memory");
    return value;
}
static void write_reg(unsigned offset, uint32_t value)
{
    __asm__ volatile ("dmb sy" ::: "memory");
    *(volatile uint32_t *)(uart_base + offset) = value;
}
bool uart_init(uintptr_t base, uint32_t clock, uint32_t baud)
{
    if (!base || !baud) { return false; }
    uint64_t divisor = ((uint64_t)clock * 4 + baud / 2) / baud;
    if (divisor < 64 || divisor / 64 > 65535) { return false; }
    uart_base = base;
    write_reg(CR, 0);
    while (read_reg(FR) & (1u << 3)) { }
    write_reg(IMSC, 0);
    write_reg(ICR, 0x7ff);
    write_reg(IBRD, (uint32_t)(divisor / 64));
    write_reg(FBRD, (uint32_t)(divisor % 64));
    write_reg(LCRH, (3u << 5) | (1u << 4)); /* 8N1, FIFO enabled */
    write_reg(CR, (1u << 9) | (1u << 8) | 1u); /* RX, TX, UART enable */
    __asm__ volatile ("dsb sy" ::: "memory");
    return true;
}
bool uart_attach_firmware(uintptr_t base)
{
    if (!base || (base & 3)) { return false; }
    uart_base=base;
    uint32_t control=read_reg(CR);
    /* Pi 5 firmware configures UART10 clocks, pins and 115200 baud. Preserve
     * that setup instead of assuming its source clock equals QEMU's 24 MHz. */
    if ((control & ((1u << 8) | 1u)) != ((1u << 8) | 1u)) { return false; }
    write_reg(IMSC,0);
    write_reg(ICR,0x7ff);
    write_reg(CR,control | (1u << 9));
    __asm__ volatile ("dsb sy" ::: "memory");
    return true;
}
void uart_putc(char c)
{
    while (read_reg(FR) & (1u << 5)) { }
    write_reg(DR, (unsigned char)c);
}
int uart_getc(void)
{
    if (read_reg(FR) & (1u << 4)) { return -1; }
    uint32_t data = read_reg(DR);
    if (data & 0xf00) { return -1; } /* Discard errored input. */
    return (int)(data & 0xff);
}
