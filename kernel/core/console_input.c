#include "console.h"
#include "crypto.h"
#include "xhci_test.h"
static bool previous_cr,damaged_line;
int console_getc(void)
{
    int c=uart_getc();
    if (c==CONSOLE_RX_ERROR) {
        damaged_line=true;
        previous_cr=false;
        return CONSOLE_NO_INPUT;
    }
    if (c==CONSOLE_NO_INPUT) { return damaged_line ? c : xhci_keyboard_getc(); }
    if (c=='\n' && previous_cr) { previous_cr=false;return CONSOLE_NO_INPUT; }
    previous_cr=c=='\r';
    if (damaged_line) {
        if (c!='\r' && c!='\n') { return CONSOLE_NO_INPUT; }
        damaged_line=false;
        return CONSOLE_RX_ERROR;
    }
    return c;
}
bool console_read_secret(unsigned char *out,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }
    if (!out || !actual || !capacity || capacity>128) { return false; }
    crypto_wipe(out,capacity);size_t n=0;bool bad=false;
    for (;;) {
        int c=console_getc();
        if (c==CONSOLE_RX_ERROR) { crypto_wipe(out,capacity);return false; }
        if (c==CONSOLE_NO_INPUT) { continue; }
        if (c=='\r' || c=='\n') {
            if (!bad && n) { *actual=n;return true; }
            crypto_wipe(out,capacity);return false;
        }
        if (c==21) { crypto_wipe(out,capacity);n=0;bad=false;continue; }
        if (c=='\b' || c==127) { if (n) { out[--n]=0; }continue; }
        if (c<32 || c>126) { bad=true;continue; }
        if (n==capacity) { bad=true;continue; }
        out[n++]=(unsigned char)c;
    }
}
