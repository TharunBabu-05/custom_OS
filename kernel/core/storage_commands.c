#include "block.h"
#include "sdhci.h"
#include "vfs.h"
#include "console.h"
#include "dt_property.h"
#include "wifi.h"
#include "wifi_files.h"
#include "dhcp.h"
#include "netprobe.h"
#include "sha256.h"
#include "network_worker.h"
#include "rp1_probe.h"
static struct block_device disk;
static bool attached;
static uintptr_t firmware_dtb;
static const char *board_name;
static char wifi_path[256],wifi_compatible[64];
static unsigned char wifi_mac[6];
static bool have_wifi_mac;
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static bool begins(const char *a,const char *b) { while (*b) { if (*a++!=*b++) { return false; } }return true; }
static const char digits[]="0123456789abcdef";
static void print_digest(const unsigned char digest[32])
{
    for (unsigned i=0;i<32;++i) { console_putc(digits[digest[i]>>4]);console_putc(digits[digest[i]&15]); }
}
struct export_stream { struct sha256 hash;unsigned offset; };
static bool export_hex(const void *data,size_t bytes,void *context)
{
    struct export_stream *s=context;const unsigned char *p=data;
    if (!s->offset) { console_write("-----BEGIN OUROS DATA BACKUP V1-----\n"); }
    sha256_update(&s->hash,data,bytes);
    for (unsigned i=0;i<bytes;i+=32) {
        console_putc('D');for (int shift=28;shift>=0;shift-=4) { console_putc(digits[(s->offset>>shift)&15]); }console_putc(':');
        for (unsigned j=0;j<32;++j) { console_putc(digits[p[i+j]>>4]);console_putc(digits[p[i+j]&15]); }
        s->offset+=32;console_putc('\n');
    }return true;
}
void storage_setup(const char *board,uintptr_t dtb)
{
    board_name=board; firmware_dtb=dtb; const unsigned char *p; uint32_t n;
    if (!dtb || !dt_property((const void *)dtb,0x200000,"/aliases","wifi0",&p,&n) ||
        n<2 || n>sizeof(wifi_path) || p[n-1]) { return; }
    for (uint32_t i=0;i<n;++i) { wifi_path[i]=(char)p[i]; }
    if (dt_property((const void *)dtb,0x200000,wifi_path,"compatible",&p,&n)) {
        uint32_t i=0; while (i<n && i+1<sizeof(wifi_compatible) && p[i]) { wifi_compatible[i]=(char)p[i]; ++i; }
        wifi_compatible[i]='\0';
    }
    if (dt_property((const void *)dtb,0x200000,wifi_path,"local-mac-address",&p,&n) && n==6) {
        for (unsigned i=0;i<6;++i) { wifi_mac[i]=p[i]; } have_wifi_mac=true;
    }
}
bool storage_mount_boot(void)
{
    if (!ramfs_get_stats().ready) { console_write("Storage mount requires an available RAM root.\n"); return false; }
    if (!attached) { attached=platform_storage_attach(&disk,firmware_dtb); }
    if (!attached) {
        console_write("Storage attachment failed; /boot stays unmounted. No disk writes were issued.\n");
        platform_storage_info(); return false;
    }
    if (!vfs_boot_mounted()) {
        enum fs_result r=vfs_mount_boot(&disk);
        if (r!=FS_OK) { console_write("FAT mount failed: "); console_write(ramfs_error(r)); console_write("\n"); }
    }
    vfs_boot_info();
    return vfs_boot_mounted();
}
bool storage_mount_data(void)
{
    if (!attached || !vfs_boot_mounted()) { console_write("Run storageinit before datainit.\n");return false; }
    if (!vfs_data_mounted()) {
        enum fs_result r=vfs_mount_data(&disk);
        if (r!=FS_OK) {
            console_write("Data mount failed: ");console_write(ramfs_error(r));console_write(". No formatting attempted.\n");
            vfs_data_info();platform_storage_info();return false;
        }
    }
    vfs_data_info();
    return !vfs_data_failed();
}
static const char *bank_state(enum persist_bank_state state)
{
    static const char *const states[]={
        [PERSIST_BANK_EMPTY]="empty", [PERSIST_BANK_BAD_HEADER]="invalid header",
        [PERSIST_BANK_BAD_PAYLOAD]="invalid payload", [PERSIST_BANK_UNREADABLE]="unreadable",
        [PERSIST_BANK_VALID]="valid"
    };
    return states[state];
}
static void data_check(void)
{
    struct persist_report report;
    enum fs_result result=vfs_data_check(&report);
    if (result!=FS_OK) {
        console_write("Data check unavailable: ");console_write(ramfs_error(result));console_write("\n");
        return;
    }
    console_write("Mounted /data bank=");console_u64(report.mounted_bank);
    console_write(" generation=");console_u64(report.mounted_generation);console_write("\n");
    for (unsigned bank=0;bank<2;++bank) {
        const struct persist_bank_report *info=&report.banks[bank];
        console_write("bank=");console_u64(bank);console_write(" status=");console_write(bank_state(info->state));
        if (info->generation) { console_write(" generation=");console_u64(info->generation); }
        if (info->state==PERSIST_BANK_UNREADABLE) {
            console_write(" lba=");console_u64(info->lba);console_write(" result=");console_write(block_error(info->io));
        }
        if (bank==report.mounted_bank) {
            console_write(info->matches_mounted ? " matches_mounted=yes" : " matches_mounted=no");
        }
        console_write("\n");
    }
}
static void path_version(const char *label,unsigned bank,const struct persist_path_history *history)
{
    const struct persist_bank_report *info=&history->snapshots.banks[bank];
    const struct persist_path_entry *entry=&history->entries[bank];
    bool mounted=bank==history->snapshots.mounted_bank;
    bool available=mounted ? info->matches_mounted :
        info->state==PERSIST_BANK_VALID && info->generation<history->snapshots.mounted_generation;
    console_write(label);console_write(" bank=");console_u64(bank);
    if (info->generation) { console_write(" generation=");console_u64(info->generation); }
    console_write(" status=");console_write(bank_state(info->state));
    if (!available) { console_write(" path=unavailable"); }
    else if (entry->kind==PERSIST_PATH_FILE) {
        console_write(" path=file bytes=");console_u64(entry->bytes);
        console_write(" sha256=");print_digest(entry->digest);
    } else { console_write(entry->kind==PERSIST_PATH_DIRECTORY ? " path=directory" : " path=missing"); }
    console_write("\n");
}
static void data_history(const char *path)
{
    struct persist_path_history history;
    enum fs_result result=vfs_data_history(path,&history);
    if (result!=FS_OK) {
        console_write("Data history unavailable: ");console_write(ramfs_error(result));console_write("\n");
        return;
    }
    console_write("Data history ");console_write(path);console_write("\n");
    path_version("mounted",history.snapshots.mounted_bank,&history);
    path_version("previous",history.snapshots.mounted_bank^1,&history);
}
static bool data_word(const char **line,char out[RAMFS_PATH_MAX])
{
    while (**line==' ') { ++*line; }
    unsigned n=0;
    while (**line && **line!=' ') {
        if (n==RAMFS_PATH_MAX-1) { return false; }
        out[n++]=*(*line)++;
    }
    out[n]=0;
    while (**line==' ') { ++*line; }
    return n!=0;
}
bool storage_command(const char *line)
{
    if (same(line,"datacheck")) { data_check();return true; }
    if (same(line,"datacut")) {
        enum fs_result result=vfs_data_interruption_test();
        if (result==FS_OK) { console_write("CUT READY: new data flushed; index unpublished. Power off the spare-media Pi now.\n"); }
        else { console_write("Spare-media cut test refused/failed: ");console_write(ramfs_error(result));console_write("\n"); }
        return true;
    }
    if (same(line,"datahistory") || begins(line,"datahistory ")) {
        const char *args=line+11;char path[RAMFS_PATH_MAX];
        if (data_word(&args,path) && !*args) { data_history(path); }
        else { console_write("Usage: datahistory /data/PATH (v5)\n"); }
        return true;
    }
    if (same(line,"datarecover") || begins(line,"datarecover ")) {
        const char *args=line+11;char from[RAMFS_PATH_MAX],to[RAMFS_PATH_MAX];
        if (!data_word(&args,from) || !data_word(&args,to) || *args) {
            console_write("Usage: datarecover /data/OLD /data/NEW (v5, new destination)\n");
            return true;
        }
        uint64_t generation;
        enum fs_result result=vfs_data_recover(from,to,&generation);
        if (result==FS_OK) {
            console_write("Recovered generation ");console_u64(generation);
            console_write(" to ");console_write(to);console_write("\n");
        } else {
            console_write("Data recovery refused/failed: ");console_write(ramfs_error(result));console_write("\n");
            if (vfs_data_failed()) { vfs_data_info();platform_storage_info(); }
        }
        return true;
    }
    bool previous=same(line,"dataexport previous");
    if (previous || same(line,"dataexport")) {
        struct export_stream stream={0};sha256_init(&stream.hash);
        enum fs_result r=previous ? vfs_data_export_previous(export_hex,&stream) : vfs_data_export(export_hex,&stream);
        if (r==FS_OK) {
            unsigned char hash[32];sha256_final(&stream.hash,hash);console_write("OUROS-DATA-SHA256 ");
            print_digest(hash);
            console_write("\n-----END OUROS DATA BACKUP V1-----\nData export complete; files unchanged.\n");
        } else if (previous && r==FS_NOT_FOUND) {
            console_write("No older committed snapshot available.\n");
        } else { console_write("Data export failed: ");console_write(ramfs_error(r));console_write("\n"); }return true;
    }
    if (same(line,"datarestore") || begins(line,"datarestore ")) {
        const char *path=line+11;while (*path==' ') { ++path; }
        if (!*path) { console_write("Usage: datarestore /boot/DATA.BAK (empty /data, same format only)\n");return true; }
        enum fs_result r=vfs_data_restore(path);
        console_write(r==FS_OK ? "Data restore committed and read back; run datainfo and verify files.\n" : "Data restore refused/failed: ");
        if (r!=FS_OK) { console_write(ramfs_error(r));console_write("; requires empty healthy /data and a valid compatible boot backup.\n"); }
        if (vfs_data_failed()) { vfs_data_info();platform_storage_info(); }return true;
    }
    if (same(line,"datainit")) { (void)storage_mount_data();return true; }
    if (same(line,"datainfo")) { vfs_data_info();return true; }
    if (same(line,"sync")) {
        enum fs_result r=vfs_data_sync();console_write(r==FS_OK ? "Data cache flushed; file updates already commit synchronously.\n" : "Data sync failed: ");
        if (r!=FS_OK) { console_write(ramfs_error(r));console_write("\n");if (vfs_data_failed()) { vfs_data_info();platform_storage_info(); } }return true;
    }
    if (same(line,"nettest")) { console_write(dhcp_selftest() ? "PASS: Ethernet/IPv4/UDP/DHCP public packet fixtures; no network I/O.\n" : "FAIL: DHCP packet fixtures.\n");console_write(netprobe_selftest() ? "PASS: ARP/ICMP gateway public packet fixtures; no network I/O.\n" : "FAIL: ARP/ICMP packet fixtures.\n");console_write(dns_selftest() ? "PASS: DNS A query/compressed-answer public fixtures; no network I/O.\n" : "FAIL: DNS packet fixtures.\n");return true; }
#ifndef OUROS_PI5
    if (same(line,"tcpmodeltest")) {
        console_write(tcp_worker_test() ? "PASS: TCP worker and EL0 file transfer.\n" : "FAIL: TCP worker integration.\n");
        return true;
    }
    if (same(line,"udpmodeltest")) {
        console_write(udp_worker_test() ? "PASS: UDP worker and EL0 socket integration.\n" : "FAIL: UDP worker integration.\n");
        return true;
    }
#endif
    if (same(line,"networktest") || begins(line,"networktest ")) {
        bool el0=same(line,"networktest el0") || same(line,"networktest e10") || same(line,"networktest elO");
        if (!el0 && !same(line,"networktest")) {
            console_write("Usage: networktest [el0] (e, lowercase L, digit zero)\n");return true;
        }
        if (network_worker_active()) { console_write("Stop the active network worker first.\n");return true; }
        console_write(network_worker_test(el0) ?
            "PASS: network worker yielded, resumed, preserved its stack and stopped.\n" :
            "Network worker test did not pass. Check MMU, timer and /boot for the EL0 test.\n");
        return true;
    }
    if (same(line,"netup") || begins(line,"netup ")) {
        if (rp1_ethernet_active()) { console_write("Use ethdown.\n"); }
        else { platform_wifi_background(line[5] ? line+6 : ""); }
        return true;
    }
    if (same(line,"netping") || begins(line,"netping ")) { platform_network_ping(line[7] ? line+8 : "");return true; }
    if (same(line,"netrenew")) { platform_network_renew();return true; }
    if (same(line,"netdown")) {
        if (rp1_ethernet_active()) { console_write("Use ethdown.\n"); return true; }
        if (!network_worker_active()) { console_write("No background Wi-Fi session.\n");return true; }
        console_write(network_worker_stop() ? "Wi-Fi worker stopped. Use netinfo for cleanup status.\n" : "Wi-Fi worker still running.\n");return true;
    }
    if (network_worker_active() && begins(line,"wifi")) {
        console_write(rp1_ethernet_active() ? "Use ethdown.\n" :
                      "Background Wi-Fi owns the radio. Use netinfo or netdown.\n");return true;
    }
    if (same(line,"wifiinit")) { platform_wifi_init(firmware_dtb); return true; }
    if (same(line,"wificores")) { platform_wifi_cores(); return true; }
    if (same(line,"wififiles")) { wifi_files_command(); return true; }
    if (same(line,"wifiload")) { platform_wifi_load(); return true; }
    if (same(line,"wifistart")) { platform_wifi_start(); return true; }
    if (same(line,"wificontrol")) { platform_wifi_control(); return true; }
    if (same(line,"wificaps")) { platform_wifi_caps(); return true; }
    if (same(line,"wifiscan")) { platform_wifi_scan(); return true; }
    if (same(line,"wifitarget")) { platform_wifi_target(""); return true; }
    if (begins(line,"wifitarget ")) { platform_wifi_target(line+11);return true; }
    if (same(line,"wifiauthprep")) { platform_wifi_auth_prepare("");return true; }
    if (begins(line,"wifiauthprep ")) { platform_wifi_auth_prepare(line+13);return true; }
    if (same(line,"wifijoinprobe")) { platform_wifi_join_probe("");return true; }
    if (begins(line,"wifijoinprobe ")) { platform_wifi_join_probe(line+14);return true; }
    if (same(line,"wifisaeprobe")) { platform_wifi_sae_probe("");return true; }
    if (begins(line,"wifisaeprobe ")) { platform_wifi_sae_probe(line+13);return true; }
    if (same(line,"wifisaeauth")) { platform_wifi_sae_auth("");return true; }
    if (begins(line,"wifisaeauth ")) { platform_wifi_sae_auth(line+12);return true; }
    if (same(line,"wifiassoc")) { platform_wifi_assoc("");return true; }
    if (begins(line,"wifiassoc ")) { platform_wifi_assoc(line+10);return true; }
    if (same(line,"wifidhcp")) { platform_wifi_dhcp("");return true; }
    if (begins(line,"wifidhcp ")) { platform_wifi_dhcp(line+9);return true; }
    if (same(line,"wifiping")) { platform_wifi_ping("");return true; }
    if (begins(line,"wifiping ")) { platform_wifi_ping(line+9);return true; }
    if (same(line,"wificonnect")) { platform_wifi_connect("");return true; }
    if (begins(line,"wificonnect ")) { platform_wifi_connect(line+12);return true; }
    if (same(line,"netinfo")) { platform_network_info();return true; }
    if (same(line,"wifiprepare")) { platform_wifi_prepare(""); return true; }
    if (begins(line,"wifiprepare ")) { platform_wifi_prepare(line+12);return true; }
    if (same(line,"wifiprobe")) { platform_wifi_probe(); return true; }
    if (same(line,"storageinit")) { (void)storage_mount_boot();return true; }
    if (same(line,"storagetest")) {
        if (!attached) { console_write("Run storageinit first.\n"); return true; }
        unsigned char sector[512]; uint32_t hash=2166136261u;
        uint64_t count=disk.capacity_known && disk.sectors<512 ? disk.sectors : 512;
        for (uint64_t lba=0;lba<count;++lba) {
            enum block_result r=block_read(&disk,lba,sector);
            if (r!=BLOCK_OK) { console_write("Storage read test failed: "); console_write(block_error(r)); console_write("\n"); return true; }
            for (unsigned i=0;i<512;++i) { hash=(hash^sector[i])*16777619u; }
        }
        console_write("PASS: read-only storage sectors="); console_u64(count);
        console_write(" fnv1a32="); console_hex(hash); console_write("\n"); return true;
    }
    if (same(line,"sdtest") || same(line,"sdscan") || same(line,"sdscan tail")) {
#ifdef OUROS_PI5
        pi5_sd_test((const void *)firmware_dtb,line[2]=='t' ? 1 : SDHCI_TEST_MAX_SECTORS,line[6]==' ');
#else
        console_write("microSD read test requires a Raspberry Pi 5.\n");
#endif
        return true;
    }
    if (same(line,"sdprobe")) {
#ifdef OUROS_PI5
        pi5_sd_probe((const void *)firmware_dtb);
#else
        console_write("microSD host probe requires a Raspberry Pi 5.\n");
#endif
        return true;
    }
    if (same(line,"sdinfo")) {
#ifdef OUROS_PI5
        pi5_sd_info();
#else
        console_write("microSD host status requires a Raspberry Pi 5.\n");
#endif
        return true;
    }
    if (same(line,"storageinfo")) {
        platform_storage_info();
        if (attached) {
            console_write("device="); console_write(disk.name); console_write(" sector_size=512 boot=read-only data=opt-in\n");
            if (disk.capacity_known) { console_write("Capacity: "); console_memory_size(disk.sectors*512); console_write("\n"); }
            else { console_write("Card capacity not discovered; SDHC 32-bit sector addressing limit only.\n"); }
        }
        vfs_boot_info(); return true;
    }
    if (same(line,"wifiinfo") || same(line,"wifi")) {
        console_write("Wi-Fi state: manual firmware/control bring-up; wificonnect opens a foreground session.\n");
        if (wifi_path[0]) {
            console_write("Firmware DT Wi-Fi node: "); console_write(wifi_path);
            console_write("\nDT compatible: "); console_write(wifi_compatible); console_write("\n");
            if (have_wifi_mac) {
                static const char hex[]="0123456789abcdef";
                console_write("Firmware-described MAC: ");
                for (unsigned i=0;i<6;++i) {
                    if (i) { console_putc(':'); } console_putc(hex[wifi_mac[i]>>4]); console_putc(hex[wifi_mac[i]&15]);
                } console_write("\n");
            }
        } else { console_write("No wifi0 alias discovered in firmware DT.\n"); }
        if (board_name && board_name[0]=='r') {
            console_write("Pi 5 platform plan: BCM43455-class FullMAC over separate SDIO host 0x1001100000.\n");
            console_write("DT compatible brcm,bcm4329-fmac is a family binding, not a measured chip ID.\n");
        } else { console_write("QEMU virt has no emulated Pi Wi-Fi adapter.\n"); }
        console_write("Requested network security: WPA3-Personal (SAE + protected management frames).\n");
        console_write("Commands: wifiinfo, wifiprobe, wifiinit, wificores, wififiles, wifiload, wifistart, wificontrol.\n");
        console_write("wificaps (read features/MAC) wifiprepare <CC> (CLM + country, no scan)\n");
        console_write("wifiscan (refresh the passive scan after country preparation)\n");
        console_write("wifitarget <SSID> (cached WPA3 cipher/PMF checks; no connection)\n");
        console_write("wifiauthprep <SSID> (verify/restore firmware SAE/PMF/events; no join)\n");
        console_write("wifijoinprobe <SSID> (observe external-SAE request, then DOWN/restore; no password)\n");
        console_write("wifisaeprobe <SSID> (hidden password, fresh SAE commit/reply probe; no confirm or IP)\n");
        console_write("wifisaeauth <SSID> (hidden password, live SAE confirm test; then DOWN/restore, no association/IP)\n");
        console_write("wifiassoc <SSID> (verified SAE/M3, key-install/live-M4 test; then DOWN/delete, no persistent link/IP)\n");
        console_write("wifidhcp <SSID> (fresh SAE/M4, peer-port authorization/readback, DHCP test; DOWN/delete) netinfo\n");
        console_write("wifiping <SSID> (fresh DHCP, address checks, ARP and gateway ICMP; then DOWN)\n");
        console_write("wificonnect <SSID> (foreground network console: ping, netinfo, disconnect)\n");
        platform_wifi_status();
        console_write("netup [-r|-R] <SSID> starts background Wi-Fi. -r pins the BSSID. -R allows a changed hotspot BSSID.\n"); return true;
    }
    return false;
}
