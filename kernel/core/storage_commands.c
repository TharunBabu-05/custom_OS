#include "block.h"
#include "vfs.h"
#include "console.h"
#include "dt_property.h"
#include "wifi.h"
static struct block_device disk;
static bool attached;
static const char *board_name;
static char wifi_path[256],wifi_compatible[64];
static unsigned char wifi_mac[6];
static bool have_wifi_mac;
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
void storage_setup(const char *board,uintptr_t dtb)
{
    board_name=board; const unsigned char *p; uint32_t n;
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
    if (same(line,"wifiinit")) { platform_wifi_init(); return true; }
    if (same(line,"wifiprobe")) { platform_wifi_probe(); return true; }
    if (same(line,"storageinit")) {
        if (board_name && board_name[0]=='r') {
            console_write("Pi storageinit targets the microSD slot, not USB storage.\n");
            console_write("For a USB boot pendrive, use usbinfo/usbprobe; USB disk I/O is pending.\n");
        }
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
        console_write("Wi-Fi state: no FullMAC/firmware driver; scan/connect/IP unavailable.\n");
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
        console_write("Commands: wifiinfo, wifiprobe, wifiinit. No scan/connect command is available yet.\n");
        platform_wifi_status();
        console_write("Required next: powered SDIO CMD52/53, chip identification, board firmware,\n");
        console_write("FullMAC protocol, SAE/PMF authentication, IP/DHCP/DNS; no network connection yet.\n"); return true;
    }
    return false;
}
