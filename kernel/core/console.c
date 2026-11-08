#include "console.h"
void console_putc(char c)
{
    if (c == '\n') { uart_putc('\r'); }
    uart_putc(c);
}
void console_write(const char *text)
{
    while (*text) { console_putc(*text++); }
}
void console_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> shift) & 15]);
    }
}
void console_u64(uint64_t value)
{
    char digits[20];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) { console_putc(digits[--count]); }
}
static void size_fraction(uint64_t bytes, uint64_t unit)
{
    console_u64(bytes/unit);
    console_putc('.');
    uint64_t fraction=(bytes%unit)*1000/unit;
    console_putc((char)('0'+fraction/100));
    console_putc((char)('0'+fraction/10%10));
    console_putc((char)('0'+fraction%10));
}
void console_memory_size(uint64_t bytes)
{
    size_fraction(bytes,1000000); console_write(" MB (");
    size_fraction(bytes,1048576); console_write(" MiB)");
}
