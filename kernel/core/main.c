#include "board.h"
#include "console.h"
#include "terminal.h"
#include "dt_property.h"
#include "dt_resources.h"
#include "resources.h"
#include "rp1_probe.h"
#include "rp1_dma_test.h"
#include "cpu.h"
#include "arm_platform.h"
#include "memory.h"
#include "heap.h"
#include "mmu.h"
#include "exception.h"
#include "irq.h"
#include "task.h"
#include "kfile.h"
#include "process.h"
#include "ramfs.h"
#include "reboot.h"
#include "block.h"
#include "usb.h"
#include "crypto.h"
#include "p256.h"
#include "sae_h2e.h"
#include "sae_session.h"
#include "rng200.h"
#include "wpa_keys.h"
#include "network_worker.h"
#include "trust_time.h"
#include "framebuffer.h"
#include "platform_health.h"
#include "pi5_pcie.h"
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
    irq_fatal_stop();
    if (console_ready) {
        console_write("\nFATAL exception vector="); console_u64(vector);
        console_write(" ESR="); console_hex(esr);
        console_write(" ELR="); console_hex(elr);
        console_write(" FAR="); console_hex(far);
        console_write("\nCPU halted.\n");
    }
    cpu_halt();
}
void exception_frame_fatal(const struct exception_frame *frame)
{
    if (console_ready) {
        console_write("\nException class: ");unsigned kind=(unsigned)frame->vector&3;
        console_write(kind==0 ? exception_class(frame->esr) : kind==1 ? "IRQ" : kind==2 ? "FIQ" : "SError");console_write(" SPSR=");console_hex(frame->spsr);console_write(" SP=");console_hex((uintptr_t)frame+sizeof(*frame));console_write("\n");
        for (unsigned i=0;i<31;++i) { console_write("x");console_u64(i);console_write("=");console_hex(frame->x[i]);console_write(i%4==3 ? "\n" : " "); }console_write("\n");
    }
    exception_fatal(frame->vector,frame->esr,frame->elr,frame->far);
}
static bool math_command(const char *line)
{
    static const struct {
        const char *name,*force,*description,*pass;bool (*run)(void);
    } tests[]={
        {"p256test","p256test force","P-256 public vectors", "PASS: P-256 RFC5903 public/shared points, scalar boundaries and group identities.\n",p256_selftest},
        {"h2etest","h2etest force","SAE H2E public fixtures", "PASS: SAE H2E group-19 mapping, password token and MAC-bound password element.\n",sae_h2e_selftest},
        {"saetest","saetest force","in-memory SAE public exchange", "PASS: SAE commit/confirm, key derivation, bad-confirm rejection and gated key export.\n",sae_session_selftest}
    };
    static unsigned char results[3];
    for (unsigned i=0;i<3;++i) {
        bool force=equal(line,tests[i].force);
        if (!force && !equal(line,tests[i].name)) { continue; }
        if (results[i] && !force) { console_write("Cached self-test result from this boot; append ' force' to rerun.\n"); }
        else {
            console_write("Testing ");console_write(tests[i].description);console_write("; this can take minutes with caches off. Wait for the prompt.\n");
            uint64_t start=timer_ticks();results[i]=tests[i].run() ? 1 : 2;
            uint64_t elapsed=timer_ticks()-start,hz=timer_frequency();
            console_write("Self-test elapsed_seconds=");console_u64(hz ? elapsed/hz : 0);console_write("\n");
        }
        console_write(results[i]==1 ? tests[i].pass : "FAIL: math self-test; authentication must stay unavailable.\n");
        console_write("Public fixtures only; no hotspot credentials, radio exchange or network connection.\n");return true;
    }
    return false;
}
/* Firmware may add its own words to /chosen/bootargs. Only a single, exact
 * startup setting enables automatic controller takeover. */
static bool shell_requested(uintptr_t dtb)
{
    const unsigned char *args;
    uint32_t length;
    if (!dtb || !dt_property((const void *)dtb,0x200000,"/chosen","bootargs",&args,&length) ||
        !length || args[length-1]!=0) { return false; }
    const char key[]="ouros.startup=";
    const char setting[]="ouros.startup=shell";
    bool selected=false;
    uint32_t pos=0;
    while (pos<length-1) {
        unsigned char c=args[pos];
        if (c==' ' || c=='\t') { ++pos;continue; }
        uint32_t first=pos;
        while (pos<length-1 && args[pos]!=' ' && args[pos]!='\t') {
            if (!args[pos]) { return false; }
            ++pos;
        }
        uint32_t n=pos-first;
        uint32_t matched=0;
        while (matched<n && setting[matched] && args[first+matched]==(unsigned char)setting[matched]) { ++matched; }
        if (matched<sizeof(key)-1) { continue; }
        if (selected || setting[matched] || matched!=n) { return false; }
        selected=true;
    }
    return selected;
}
static void startup(void)
{
    console_write("Starting serial shell from /boot/SHELL.ELF\n");
    if (!storage_mount_boot()) { console_write("Startup stopped at /boot.\n");return; }
    if (!storage_mount_data()) { console_write("Startup stopped at /data.\n");return; }
    if (!mmu_ensure_enabled()) { console_write("Startup stopped at MMU setup.\n");return; }
    if (!irq_ensure_periodic()) { console_write("Startup stopped at timer setup.\n");return; }
    if (process_shell()!=0) { console_write("Startup shell could not run or ended unsuccessfully.\n"); }
}
static void command(const char *line)
{
    if (equal(line,"resinfo")) { resource_report(); return; }
    if (equal(line,"pcieinfo") || equal(line,"pcieinfo rp1")) {
        pi5_pcie_command((const void *)firmware_dtb,line[8] ? PI5_PCIE_RP1 : PI5_PCIE_EXTERNAL,false);
        return;
    }
    if (equal(line,"pciewin") || equal(line,"pciewin rp1")) {
        pi5_pcie_command((const void *)firmware_dtb,line[7] ? PI5_PCIE_RP1 : PI5_PCIE_EXTERNAL,true);
        return;
    }
    if (equal(line,"ethprobe") || equal(line,"ethprobe reset")) {
        rp1_ethernet_command(firmware_dtb,board.uart_base,line[8]==' ' ? ETH_PULSE : ETH_PROBE); return;
    }
    if (equal(line,"ethup") || equal(line,"ethdown")) {
        rp1_ethernet_command(firmware_dtb,board.uart_base,line[3]=='u' ? ETH_UP : ETH_DOWN); return;
    }
    if (equal(line,"ethrecover")) { rp1_ethernet_command(firmware_dtb,board.uart_base,ETH_RECOVER); return; }
    if (equal(line,"ethping")) { rp1_ethernet_command(firmware_dtb,board.uart_base,ETH_PING); return; }
    if (rp1_ethernet_address_command(line)) { return; }
    if (equal(line,"ethudpup")) { rp1_ethernet_command(firmware_dtb,board.uart_base,ETH_UDP_UP); return; }
    if (equal(line,"ethtcpup")) { rp1_ethernet_command(firmware_dtb,board.uart_base,ETH_TCP_UP); return; }
    if (equal(line,"ethloop") || equal(line,"etharp")) {
        rp1_ethernet_command(firmware_dtb,board.uart_base,line[3]=='l' ? ETH_LOOP : ETH_ARP); return;
    }
    if (rp1_command(line,firmware_dtb,board.uart_base)) { return; }
    if (rp1_gpio_command(line,firmware_dtb,board.uart_base)) { return; }
    if (rp1_dma_test_command(line,firmware_dtb,board.uart_base)) { return; }
    if (equal(line,"startup")) { startup();return; }
    if (process_command(line) || task_command(line) || file_command(line) || trust_time_command(line)) { return; }
    if (exception_command(line) || irq_command(line)) { return; }
    if (mmu_command(line)) { return; }
    if (arm_platform_command(line,(const void *)firmware_dtb)) { return; }
    if (device_inventory_command(line,(const void *)firmware_dtb,&board)) { return; }
    if (platform_health_command(line,(const void *)firmware_dtb,
                                equal(board.name,"rpi5-header") || equal(board.name,"rpi5"))) { return; }
    if (framebuffer_command(line)) { return; }
    if (heap_command(line)) { return; }
    if (filesystem_command(line)) { return; }
    if (storage_command(line)) { return; }
    if (usb_command(line)) { return; }
    if (math_command(line)) { return; }
    if (equal(line,"cryptotest")) { console_write(crypto_selftest() ? "PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.\n" : "FAIL: crypto known-answer tests; do not use authentication primitives.\n");return; }
    if (equal(line,"keytest")) { console_write(wpa_keys_selftest() ? "PASS: AES-128, CMAC, key unwrap, SAE PTK, offline M2/M4, key-data and M3-retry known-answer tests.\n" : "FAIL: WPA key primitives; live handshake unavailable.\n");console_write("Public fixtures only; no key installation or EAPOL transmission.\n");return; }
    if (equal(line,"rngtest")) { platform_rng_test(firmware_dtb);return; }
    if (equal(line,"help")) {
        console_write("help info uartinfo cpuinfo meminfo uptime echo <text> clear reboot poweroff halt fault\n");
        console_write("cpulist cpustate cacheinfo atomictest\n");
        console_write("platforminfo therminfo powerinfo rtcinfo rtcset <UTC> rtcsync rtcalarm\n");
        console_write("devlist [DT path|alias] | devinfo <DT path|alias> (device tree)\n");
        console_write("mediainfo (device-tree media controllers)\n");
        console_write("resinfo (active hardware resource claims)\n");
        console_write("pcieinfo [rp1] | pciewin [rp1] (Pi 5 device tree)\n");
        console_write("rp1info ethprobe [reset] ethup ethdown ethrecover ethloop etharp ethping ethaddr [IPv4/prefix] ethudpup ethtcpup\n");
        console_write("rp1msiinfo (Pi 5 PCIe MSI-X capability and doorbell)\n");
        console_write("rp1dmatest (Pi 5 DMA scratch copy)\n");
        console_write("rp1irqtest (GIC edge dispatch on test INTID)\n");
        console_write("gpioinfo <0..27> | gpiotest <0..27> | gpiopull <0..27> | gpioloop\n");
        console_write("utc | utc set <YYYY-MM-DDTHH:MM:SSZ> | utc clear\n");
        console_write("trustlist trustadd <SHA-256> trustcheck <SHA-256> <from UTC> <to UTC> trustclear\n");
        console_write("startup (mount boot and data, enable MMU and timer, launch the serial shell)\n");
        console_write("memmap pagealloc pagefree <slot> pagetest\n");
        console_write("mmuinfo mmuinit (enable identity protection; caches off) mmutest\n");
        console_write("fbinfo fbtest fbon fboff (firmware HDMI framebuffer and UART mirror)\n");
        console_write("excinfo exctest irqinit irqinfo irqtest timerstart timerinfo timertest timerstop\n");
        console_write("taskinfo schedtest (three cooperative kernel tasks; requires MMU and timer IRQs)\n");
        console_write("appinfo apptest apprun <ELF> [up to four more] (EL0; requires MMU/timer)\n");
        console_write("svc | svc start <ELF> [arguments] | svc stop\n");
        console_write("appexec <ELF> [arguments] tooltest (EL0 utilities and sleeping processes)\n");
        console_write("appshell | appterm <1..300 seconds> <ELF> [arguments] (interactive EL0 serial session)\n");
        console_write("appinstall <source ELF> </data/new-file> (validated ELF, no overwrite)\n");
        console_write("appupdate <source ELF> </data/existing-file> (validated atomic update)\n");
        console_write("appadd | appupgrade <name> <version> <ELF>; applist; appstart <name> [arguments]\n");
        console_write("approllback | appuninstall <name> (managed applications)\n");
        console_write("taskiotest (cooperative file I/O and automatic owner cleanup) fileinfo filetest\n");
        console_write("fopen <path> r|rw|w | fread <handle> <1..512> | fwrite <handle> <text>\n");
        console_write("fseek <handle> <signed offset> set|cur|end | fclose <handle>\n");
        console_write("datainit datainfo sync (persistent /data on the preformatted data partition)\n");
        console_write("datacheck (read both on-disk snapshots)\n");
        console_write("datahistory /data/PATH | datarecover /data/OLD /data/NEW (v5)\n");
        console_write("heapinfo heaptest heapalloc <1..4096> heapfree <slot>\n");
        console_write("cryptotest (published vectors) rngtest (manual hardware source, no sample output)\n");
        console_write("keytest (offline AES/CMAC/unwrap/PTK/M2/M4/key-data/M3-retry vectors; no radio)\n");
        console_write("nettest (offline DHCP/ARP/ICMP/DNS fixtures) wifidhcp <SSID> (DHCP test) wifiping <SSID> (gateway test) netinfo\n");
        console_write("wificonnect <SSID> (foreground network console; ping IPv4/hostname, nslookup, netinfo, disconnect)\n");
        console_write("netup [-r|-R] <SSID> netping [IPv4] netrenew netinfo netdown (background Wi-Fi)\n");
        console_write("networktest [el0] (cooperative worker check)\n");
        console_write("p256test (SAE group-19 arithmetic/public vectors; no authentication)\n");
        console_write("h2etest (H2E token/password-element public fixtures; no connection)\n");
        console_write("saetest (in-memory SAE commit/confirm public exchange; no radio)\n");
        console_write("Math tests cache PASS/FAIL until reboot; append ' force' to rerun.\n");
        console_write("pwd ls [path] cd [path] cat <path> mkdir <path> touch <path>\n");
        console_write("write <path> <text> rm <path> rmdir <path> fsinfo\n");
        console_write("storageinit storageinfo storagetest sdinfo sdprobe sdtest sdscan [tail] wifi wifiinfo wifiprobe wifiinit wificores\n");
        console_write("wififiles (verify /boot assets) wifiload (upload/readback; radio CPU stays halted)\n");
        console_write("wifistart (release verified firmware; check shared memory; no connection yet)\n");
        console_write("wificontrol (F2/SDPCM and read-only firmware version queries)\n");
        console_write("wificaps (features/MAC) wifiprepare <CC> (verify/load CLM, set/read country)\n");
        console_write("wifiscan (refresh the passive scan after preparation)\n");
        console_write("wifitarget <SSID> (cached WPA3 cipher/PMF preflight; no association)\n");
        console_write("wifiauthprep <SSID> (temporary firmware SAE/PMF/event test, then restore)\n");
        console_write("wifijoinprobe <SSID> (bounded external-SAE request probe; then DOWN/restore)\n");
        console_write("wifisaeprobe <SSID> (hidden password, one fresh commit/reply, then DOWN/restore)\n");
        console_write("wifisaeauth <SSID> (hidden password, live SAE confirms, then DOWN/restore; no IP)\n");
        console_write("wifiassoc <SSID> (hidden password, verified SAE/M3, key-install/live-M4 test; then DOWN/delete)\n");
        console_write("usbinfo usbprobe (discovery diagnostics)\n");
        console_write("usbdevices (cached descriptors) usbports (live USB ports)\n");
        console_write("usbidentify | usbkeytest [usb0|usb1] <USB2 port>\n");
        console_write("usbcheck (after storageinit; read-only ring reuse and disk check)\n");
        console_write("mv <source-file> <new-path> (same filesystem; no overwrite) | stat <path>\n");
        console_write("replace <source-file> <target> (atomic publication; consumes source)\n");
        console_write("dataexport [previous] | datarestore /boot/DATA.BAK (empty /data; v5 accepts v4 archives)\n");
        console_write("usbtest (takes over one USB controller, tests DMA, halts it; reboot to repeat)\n");
        console_write("usbenum (direct USB2 device descriptors; reboot before usbtest/usbenum reuse)\n");
    } else if (equal(line,"info")) {
        console_write("OurOS development kernel | board="); console_write(board.name);
        console_write(" | EL"); console_u64(cpu_el());
        console_write(" | entered EL"); console_u64(entry_el);
        console_write("\nFirmware DTB pointer="); console_hex(firmware_dtb);
        console_write("\nSingle core; polling UART; MMU ");console_write(mmu_enabled() ? "on" : "off");console_write("; no user processes.\n");
        console_write(ramfs_get_stats().ready ? "Root filesystem: RAMFS (volatile).\n" : "Root filesystem unavailable.\n");
    } else if (equal(line,"uartinfo")) {
        struct uart_rx_status status=uart_rx_status();
        console_write(status.fifo ? "PL011 FIFO=on" : "PL011 FIFO=off");
        console_write(" rx_bytes=");console_u64(status.bytes);
        console_write(" framing=");console_u64(status.framing);
        console_write(" parity=");console_u64(status.parity);
        console_write(" breaks=");console_u64(status.breaks);
        console_write(" overruns=");console_u64(status.overruns);console_write("\n");
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
        const char *escape="\033[2J\033[H";
        while (*escape) { uart_putc(*escape++); }
        framebuffer_console_clear();
    } else if (equal(line,"reboot") || equal(line,"poweroff")) {
        bool poweroff=line[0]=='p';
        if (network_worker_stop()) { system_power_transition(board.name,firmware_dtb,poweroff); }
        else { console_write("Power transition refused: network worker active.\n"); }
    } else if (equal(line,"halt")) {
        if (!network_worker_stop()) { console_write("Halt refused: Wi-Fi worker still running.\n");return; }
        irq_fatal_stop();
        console_write("CPU halted. Reset the board or close the emulator.\n"); cpu_halt();
    } else if (equal(line,"fault")) {
        if (!network_worker_stop()) { console_write("Fault test refused: Wi-Fi worker still running.\n");return; }
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
    bool header=equal(board.name,"rpi5-header");
    const struct resource_range uart[]={
        {RESOURCE_MMIO,0,board.uart_base,0x100}, {RESOURCE_GPIO,RP1_GPIO_BASE,14,2}
    };
    const struct resource_range hat={RESOURCE_GPIO,RP1_GPIO_BASE,0,2};
    if (!resource_acquire("serial-console",uart,header ? 2 : 1) ||
        (header && !resource_acquire("hat-id",&hat,1))) { cpu_halt(); }
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
    mmu_setup(board.name,dtb);
    irq_setup(board.name,dtb);
    console_write(pages_stats().ready ? "Physical page allocator ready; type meminfo or memmap.\n" :
        "Physical page allocator disabled; type meminfo for the reason.\n");
    console_write(heap_init() ? "Kernel heap ready; type heapinfo or heaptest.\n" : "Kernel heap disabled: physical allocator unavailable.\n");
    enum fs_result fs=ramfs_init(board.name);
    if (fs==FS_OK) { console_write("Root filesystem ready: RAMFS at / (volatile).\n"); }
    else { console_write("Root filesystem unavailable: "); console_write(ramfs_error(fs)); console_write("\n"); }
    storage_setup(board.name,dtb);
    usb_setup(board.name,dtb,board.uart_base);
    if (shell_requested(dtb)) {
        startup();
    } else {
        console_write("Storage is opt-in: storageinit attempts read-only FAT32 at /boot.\n");
#ifdef OUROS_PI5
        console_write("Pi storageinit: SD handoff, or experimental read-only USB2 pendrive.\n");
        console_write("For USB storage use storageinit before usbtest/usbenum on a fresh boot.\n");
        console_write("USB discovery: usbinfo (DT), usbprobe (read-only controller registers).\n");
#endif
    }
    console_write("Kernel monitor ready. Type help.\n> ");
    struct terminal_line line;
    (void)terminal_begin(&line,TERMINAL_BYTES);
    for (;;) {
        irq_run_deferred();
        network_worker_service();
        int input=console_getc();
        if (input==CONSOLE_NO_INPUT) { process_service_step(); }
        if (input==CONSOLE_NO_INPUT || input==4) { continue; }
        unsigned previous=line.length;
        switch (terminal_feed(&line,input)) {
        case TERM_NONE: case TERM_EOF: continue;
        case TERM_ECHO: console_putc((char)input);continue;
        case TERM_ERASE: console_write("\b \b");continue;
        case TERM_CLEAR: while (previous--) { console_write("\b \b"); }continue;
        case TERM_CANCEL: console_write("^C\n");break;
        case TERM_REJECT: console_write("\nInput too long; command discarded.\n");break;
        case TERM_RX_REJECT: console_write("\nSerial receive error; input discarded.\n");break;
        case TERM_LINE: console_write("\n");command(line.bytes);break;
        }
        (void)terminal_begin(&line,TERMINAL_BYTES);
        console_write("> ");
    }
}
