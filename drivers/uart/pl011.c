/* Arm PL011 DDI0183: offsets and bits, 32-bit accesses.
 * The caller supplies the CPU physical base and UART input clock. */
#include "console.h"
#include "cpu.h"
enum { DR=0x00, ECR=0x04, FR=0x18, IBRD=0x24, FBRD=0x28, LCRH=0x2c,
       CR=0x30, IMSC=0x38, RIS=0x3c, ICR=0x44 };
enum { RX_EMPTY=1u<<4, FIFO_ENABLE=1u<<4, FRAME_ERROR=1u<<8, PARITY_ERROR=1u<<9,
       BREAK_ERROR=1u<<10, OVERRUN_ERROR=1u<<11, RX_ERRORS=0xf00 };
static uintptr_t uart_base;
static struct uart_rx_status rx;
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
void uart_drain(void)
{
    uint64_t start=timer_ticks(),limit=timer_frequency()/20;
    while ((read_reg(FR)&(1u<<3)) && timer_ticks()-start<limit) { }
}
static int receive_error(uint32_t errors)
{
    rx.framing+=(errors&FRAME_ERROR)!=0;
    rx.parity+=(errors&PARITY_ERROR)!=0;
    rx.breaks+=(errors&BREAK_ERROR)!=0;
    rx.overruns+=(errors&OVERRUN_ERROR)!=0;
    if (errors&OVERRUN_ERROR) {
        /* Queued bytes can predate the lost byte, including a line ending. */
        for (unsigned i=0;i<32 && !(read_reg(FR)&RX_EMPTY);++i) { (void)read_reg(DR); }
    }
    write_reg(ECR,0);
    write_reg(ICR,errors>>1);
    return CONSOLE_RX_ERROR;
}
int uart_getc(void)
{
    /* Detect a full-FIFO overrun before returning any earlier queued byte. */
    if (read_reg(RIS)&(OVERRUN_ERROR>>1)) { return receive_error(OVERRUN_ERROR); }
    if (read_reg(FR)&RX_EMPTY) { return CONSOLE_NO_INPUT; }
    uint32_t data = read_reg(DR);
    if (data&RX_ERRORS) { return receive_error(data&RX_ERRORS); }
    ++rx.bytes;
    return (int)(data & 0xff);
}
struct uart_rx_status uart_rx_status(void)
{
    struct uart_rx_status status=rx;
    status.fifo=(read_reg(LCRH)&FIFO_ENABLE)!=0;
    return status;
}
