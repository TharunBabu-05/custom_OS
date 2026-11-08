#ifndef OS_CONSOLE_H
#define OS_CONSOLE_H
#include <stdint.h>
#include <stdbool.h>
bool uart_init(uintptr_t base, uint32_t clock, uint32_t baud);
bool uart_attach_firmware(uintptr_t base);
void uart_putc(char c);
int uart_getc(void);
void console_putc(char c);
void console_write(const char *text);
void console_hex(uint64_t value);
void console_u64(uint64_t value);
#endif
