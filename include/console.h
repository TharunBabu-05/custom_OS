#ifndef OS_CONSOLE_H
#define OS_CONSOLE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
enum { CONSOLE_NO_INPUT=-1, CONSOLE_RX_ERROR=-2 };
struct uart_rx_status {
    uint64_t bytes,framing,parity,breaks,overruns;
    bool fifo;
};
bool uart_init(uintptr_t base, uint32_t clock, uint32_t baud);
bool uart_attach_firmware(uintptr_t base);
void uart_putc(char c);
int uart_getc(void);
struct uart_rx_status uart_rx_status(void);
void uart_drain(void);
/* Normalizes CRLF and reports RX errors after discarding through Enter. */
int console_getc(void);
/* Printable ASCII, max 128 bytes. No echo; empty/invalid/overflow input is
 * rejected and wiped. Backspace/Delete edit, Ctrl+U clears, Ctrl+C cancels
 * on Enter. Consume the whole line so rejected input cannot enter monitor. */
bool console_read_secret(unsigned char *,size_t capacity,size_t *actual);
void console_putc(char c);
void console_write(const char *text);
void console_hex(uint64_t value);
void console_u64(uint64_t value);
void console_memory_size(uint64_t bytes);
#endif
