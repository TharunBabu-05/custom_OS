#ifndef OS_CONSOLE_H
#define OS_CONSOLE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
bool uart_init(uintptr_t base, uint32_t clock, uint32_t baud);
bool uart_attach_firmware(uintptr_t base);
void uart_putc(char c);
int uart_getc(void);
/* Shared CR/LF filtering for monitor and hidden input. -1 means no char. */
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
