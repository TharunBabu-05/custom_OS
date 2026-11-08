#include "board.h"
#include "console.h"
#include "cpu.h"
static struct board_info board;
static bool console_ready;
static uint64_t boot_ticks;
static unsigned long entry_el;
static uintptr_t firmware_dtb;
void kernel_main(uintptr_t dtb, unsigned long boot_el);
void exception_fatal(unsigned long vector, unsigned long esr, unsigned long elr, unsigned long far);
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
void exception_fatal(unsigned long vector, unsigned long esr, unsigned long elr, unsigned long far)
{
    if (console_ready) {
        console_write("\nFATAL exception vector="); console_u64(vector);
        console_write(" ESR="); console_hex(esr);
        console_write(" ELR="); console_hex(elr);
        console_write(" FAR="); console_hex(far);
        console_write("\nCPU halted.\n");
    }
    cpu_halt();
}
static void command(const char *line)
{
    if (equal(line,"help")) {
        console_write("help info cpuinfo meminfo uptime echo <text> clear halt fault\n");
    } else if (equal(line,"info")) {
        console_write("OurOS development kernel | board="); console_write(board.name);
        console_write(" | EL"); console_u64(cpu_el());
        console_write(" | entered EL"); console_u64(entry_el);
        console_write("\nFirmware DTB pointer="); console_hex(firmware_dtb);
        console_write("\nSingle core; polling UART; MMU off; no user processes or filesystem.\n");
    } else if (equal(line,"cpuinfo")) {
        uint64_t midr;
        __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));
        console_write("MIDR="); console_hex(midr);
        console_write(" timer_hz="); console_u64(timer_frequency()); console_write("\n");
    } else if (equal(line,"meminfo")) {
        if (!board.ram_known) {
            console_write("RAM discovery not implemented for this board; no allocator active.\n");
            return;
        }
        console_write("DTB RAM base="); console_hex(board.ram_base);
        console_write(" bytes="); console_u64(board.ram_size);
        console_write(" (installed region; allocator not implemented)\n");
    } else if (equal(line,"uptime")) {
        uint64_t hz=timer_frequency();
        console_write("uptime_seconds=");
        console_u64(hz ? (timer_ticks()-boot_ticks)/hz : 0); console_write("\n");
    } else if (equal(line,"echo")) {
        console_write("\n");
    } else if (line[0]=='e' && line[1]=='c' && line[2]=='h' && line[3]=='o' && line[4]==' ') {
        console_write(line+5); console_write("\n");
    } else if (equal(line,"clear")) {
        console_write("\033[2J\033[H");
    } else if (equal(line,"halt")) {
        console_write("CPU halted. Reset the board or close the emulator.\n"); cpu_halt();
    } else if (equal(line,"fault")) {
        __asm__ volatile ("brk #0x42");
    } else if (*line) {
        console_write("Unknown command. Type help.\n");
    }
}
void kernel_main(uintptr_t dtb, unsigned long boot_el)
{
    entry_el=boot_el;
    firmware_dtb=dtb;
    if (!board_init(&board,dtb)) { cpu_halt(); }
    console_ready=true;
    boot_ticks=timer_ticks();
    console_write("\nOurOS: AArch64 kernel booted\n");
    console_write(board.uart_clock ? "DTB discovered PL011 at " : "Firmware configured PL011 at ");
    console_hex(board.uart_base);
    if (board.uart_clock) { console_write(" clock_hz="); console_u64(board.uart_clock); }
    else { console_write(" (baud/clock preserved)"); }
    console_write("\nBoard="); console_write(board.name);
    console_write(" entered EL"); console_u64(entry_el);
    console_write(" running EL"); console_u64(cpu_el()); console_write("\n");
    console_write("Kernel monitor ready. Type help.\n> ");
    char line[128];
    unsigned length=0;
    bool overflow=false, last_cr=false;
    for (;;) {
        int input=uart_getc();
        if (input < 0) { continue; }
        char c=(char)input;
        if (c=='\n' && last_cr) { last_cr=false; continue; }
        last_cr=c=='\r';
        if (c=='\r' || c=='\n') {
            console_write("\n");
            line[length]='\0';
            if (overflow) { console_write("Input too long; command discarded.\n"); }
            else { command(line); }
            length=0; overflow=false; console_write("> ");
        } else if (c=='\b' || c==127) {
            if (!overflow && length) { --length; console_write("\b \b"); }
        } else if (c>=32 && c<=126 && !overflow) {
            if (length+1 < sizeof(line)) { line[length++]=c; console_putc(c); }
            else { overflow=true; }
        }
    }
}
