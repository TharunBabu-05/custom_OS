#include "block.h"
#include "vfs.h"
#include "console.h"
#include "dt_property.h"
#include "wifi.h"
#include "wifi_files.h"
#include "dhcp.h"
#include "netprobe.h"
static struct block_device disk;
static bool attached;
static uintptr_t wifi_dtb;
static const char *board_name;
static char wifi_path[256],wifi_compatible[64];
static unsigned char wifi_mac[6];
static bool have_wifi_mac;
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static bool begins(const char *a,const char *b) { while (*b) { if (*a++!=*b++) { return false; } }return true; }
void storage_setup(const char *board,uintptr_t dtb)
{
    board_name=board; wifi_dtb=dtb; const unsigned char *p; uint32_t n;
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
bool storage_command(const char *line)
{
    if (same(line,"nettest")) { console_write(dhcp_selftest() ? "PASS: Ethernet/IPv4/UDP/DHCP public packet fixtures; no network I/O.\n" : "FAIL: DHCP packet fixtures.\n");console_write(netprobe_selftest() ? "PASS: ARP/ICMP gateway public packet fixtures; no network I/O.\n" : "FAIL: ARP/ICMP packet fixtures.\n");return true; }
    if (same(line,"wifiinit")) { platform_wifi_init(wifi_dtb); return true; }
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
    if (same(line,"storageinit")) {
        if (!ramfs_get_stats().ready) { console_write("Storage mount requires an available RAM root.\n"); return true; }
        if (!attached) { attached=platform_storage_attach(&disk); }
        if (!attached) {
            console_write("Storage attachment failed; /boot stays unmounted. No disk writes were issued.\n");
            platform_storage_info(); return true;
        }
        if (!vfs_boot_mounted()) {
            enum fs_result r=vfs_mount_boot(&disk);
            if (r!=FS_OK) { console_write("FAT mount failed: "); console_write(ramfs_error(r)); console_write("\n"); }
        }
        vfs_boot_info(); return true;
    }
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
    if (same(line,"storageinfo")) {
        platform_storage_info();
        if (attached) {
            console_write("device="); console_write(disk.name); console_write(" sector_size=512 access=read-only\n");
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
        console_write("wifiscan (passive scan after country preparation; one-shot results)\n");
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
        console_write("Next: verify live session and external numeric reachability; DNS, renewal/rekey and background networking remain.\n"); return true;
    }
    return false;
}
