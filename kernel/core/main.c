#include "board.h"
#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "ramfs.h"
#include "block.h"
#include "usb.h"
#include "crypto.h"
#include "rng200.h"
static struct board_info board;
static bool console_ready;
static uint64_t boot_ticks;
static unsigned long entry_el;
static uintptr_t firmware_dtb;
static uint64_t monitor_pages[16];
static bool monitor_owned[16];
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
    if (filesystem_command(line)) { return; }
    if (storage_command(line)) { return; }
    if (usb_command(line)) { return; }
    if (equal(line,"cryptotest")) { console_write(crypto_selftest() ? "PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.\n" : "FAIL: crypto known-answer tests; do not use authentication primitives.\n");return; }
    if (equal(line,"rngtest")) { platform_rng_test(firmware_dtb);return; }
    if (equal(line,"help")) {
        console_write("help info cpuinfo meminfo uptime echo <text> clear halt fault\n");
        console_write("memmap pagealloc pagefree <slot> pagetest\n");
        console_write("cryptotest (published vectors) rngtest (manual hardware source, no sample output)\n");
        console_write("pwd ls [path] cd [path] cat <path> mkdir <path> touch <path>\n");
        console_write("write <path> <text> rm <path> rmdir <path> fsinfo\n");
        console_write("storageinit storageinfo storagetest wifi wifiinfo wifiprobe wifiinit wificores\n");
        console_write("wififiles (verify /boot assets) wifiload (upload/readback; radio CPU stays halted)\n");
        console_write("wifistart (release verified firmware; check shared memory; no connection yet)\n");
        console_write("wificontrol (F2/SDPCM and read-only firmware version queries)\n");
        console_write("wificaps (features/MAC) wifiprepare <CC> (verify/load CLM, set/read country)\n");
        console_write("wifiscan (passive scan after preparation; cached one-shot results)\n");
        console_write("usbinfo usbprobe (discovery diagnostics)\n");
        console_write("usbtest (takes over one USB controller, tests DMA, halts it; reboot to repeat)\n");
        console_write("usbenum (direct USB2 device descriptors; reboot before usbtest/usbenum reuse)\n");
    } else if (equal(line,"info")) {
        console_write("OurOS development kernel | board="); console_write(board.name);
        console_write(" | EL"); console_u64(cpu_el());
        console_write(" | entered EL"); console_u64(entry_el);
        console_write("\nFirmware DTB pointer="); console_hex(firmware_dtb);
        console_write("\nSingle core; polling UART; MMU off; no user processes.\n");
        console_write(ramfs_get_stats().ready ? "Root filesystem: RAMFS (volatile).\n" : "Root filesystem unavailable.\n");
    } else if (equal(line,"cpuinfo")) {
        uint64_t midr;
        __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));
        console_write("MIDR="); console_hex(midr);
        console_write(" timer_hz="); console_u64(timer_frequency()); console_write("\n");
    } else if (equal(line,"meminfo")) {
        memory_print_info();
    } else if (equal(line,"memmap")) {
        memory_print_map();
    } else if (equal(line,"pagetest")) {
        (void)memory_page_test();
    } else if (equal(line,"pagealloc")) {
        unsigned slot=0;
        while (slot<16 && monitor_owned[slot]) { ++slot; }
        if (slot == 16) { console_write("Monitor page slots full; use pagefree <slot>.\n"); }
        else if (!page_alloc(&monitor_pages[slot])) { console_write("Allocation unavailable or out of physical pages.\n"); }
        else {
            monitor_owned[slot]=true;
            console_write("Allocated slot="); console_u64(slot);
            console_write(" physical="); console_hex(monitor_pages[slot]); console_write(" bytes=4096\n");
        }
    } else if (line[0]=='p' && line[1]=='a' && line[2]=='g' && line[3]=='e' &&
               line[4]=='f' && line[5]=='r' && line[6]=='e' && line[7]=='e' && line[8]==' ') {
        const char *p=line+9; unsigned slot=0; bool valid=*p != '\0';
        while (*p && valid) {
            if (*p<'0' || *p>'9' || slot>15) { valid=false; break; }
            slot=slot*10+(unsigned)(*p++-'0');
        }
        if (!valid || slot>=16 || !monitor_owned[slot]) { console_write("Invalid or unallocated page slot.\n"); }
        else if (!page_free(monitor_pages[slot])) { console_write("Page release rejected.\n"); }
        else { monitor_owned[slot]=false; console_write("Freed slot="); console_u64(slot); console_write("\n"); }
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
    memory_boot_init(dtb);
    console_write(pages_stats().ready ? "Physical page allocator ready; type meminfo or memmap.\n" :
        "Physical page allocator disabled; type meminfo for the reason.\n");
    enum fs_result fs=ramfs_init(board.name);
    if (fs==FS_OK) { console_write("Root filesystem ready: RAMFS at / (volatile).\n"); }
    else { console_write("Root filesystem unavailable: "); console_write(ramfs_error(fs)); console_write("\n"); }
    storage_setup(board.name,dtb);
    usb_setup(board.name,dtb,board.uart_base);
    console_write("Storage is opt-in: storageinit attempts read-only FAT32 at /boot.\n");
#ifdef OUROS_PI5
    console_write("Pi storageinit: SD handoff, or experimental read-only USB2 pendrive.\n");
    console_write("For USB storage use storageinit before usbtest/usbenum on a fresh boot.\n");
    console_write("USB discovery: usbinfo (DT), usbprobe (read-only controller registers).\n");
#endif
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
