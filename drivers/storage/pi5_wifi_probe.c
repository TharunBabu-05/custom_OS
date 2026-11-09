/* Manual SDIO bring-up, verified staging and explicit firmware startup.
 * Never called at boot. The upload keeps CR4 halted; wifistart releases it.
 * F2 supports only read-only firmware control queries; no network interface. */
#include "wifi.h"
#include "sdhci.h"
#include "console.h"
#include "cpu.h"
#include "sdio.h"
#include "wifi_power.h"
#include "brcm_sdio.h"
#include "brcm_chip.h"
#include "brcm_start.h"
#include "brcm_control.h"
#include "brcm_prepare.h"
#include "brcm_scan.h"
#include "brcm_auth.h"
#include "wifi_target.h"
#include "wifi_files.h"
#include "dt_property.h"
#define WIFI_HOST 0x1001100000UL
static struct sdio_card card={.error=BLOCK_NOT_READY};
static struct wifi_power_state power={.error=BLOCK_NOT_READY};
static struct brcm_sdio_state chip={.error=BLOCK_NOT_READY};
static struct brcm_chip_info cores={.error=BLOCK_NOT_READY};
static struct brcm_fw_state firmware={.error=BLOCK_NOT_READY};
static struct brcm_start_state startup={.error=BLOCK_NOT_READY};
static struct brcm_control_state control={.error=BLOCK_NOT_READY};
static struct brcm_caps_state capabilities={.error=BLOCK_NOT_READY};
static struct brcm_prepare_state preparation={.error=BLOCK_NOT_READY};
static struct brcm_scan_state scan={.error=BLOCK_NOT_READY};
static struct brcm_auth_probe auth_probe={.error=BLOCK_NOT_READY};
static uintptr_t firmware_dtb;
static void host_status(void);
static void startup_status(void);
static void control_status(void);
static void prepare_status(void);
static void caps_status(void);
static void scan_status(void);
static void auth_probe_status(void);
static void transfer_failure(void)
{
    const struct sdio_failure *f=&card.failure;if (!f->valid) { return; }
    static const char *const stages[]={"command","buffer-ready","fifo","transfer-end","finished"};
    console_write("SDIO first failure: ");console_write(block_error(f->error));console_write(" stage=");
    console_write((unsigned)f->stage<5 ? stages[f->stage] : "unknown");
    console_write(" CMD=");console_u64(f->command);console_write(" argument=");console_hex(f->argument);
    console_write("\nresponse=");console_hex(f->response);console_write(" IRQ=");console_hex(f->interrupt);
    if (f->command==53) { console_write((f->argument>>31) ? " direction=write" : " direction=read"); }
    console_write(" PRESENT=");console_hex(f->present);console_write("\nBLOCK_SIZE_COUNT=");console_hex(f->blocks);
    console_write(" COMMAND_MODE=");console_hex(f->control);console_write(" requested_bytes=");console_u64(f->bytes);
    console_write(" FIFO_bytes=");console_u64(f->fifo_bytes);console_write("\n");
    if (f->interrupt&(1u<<16)) { console_write("Host command timeout.\n"); }
    if (f->interrupt&(1u<<17)) { console_write("Host command CRC error.\n"); }
    if (f->interrupt&(1u<<20)) { console_write("Host data timeout.\n"); }
    if (f->interrupt&(1u<<21)) { console_write("Host data CRC error.\n"); }
    if (f->response&0xcb00) { console_write("Card R5 error bits=");console_hex(f->response&0xcb00);console_write("\n"); }
    if (f->response&0x100) { console_write("Card reported OUT_OF_RANGE.\n"); }
}
static enum block_result core_read(void *context,uint32_t address,uint32_t *value)
{ (void)context; return brcm_sdio_read32(&card,&chip,address,value); }
static enum block_result core_write(void *context,uint32_t address,uint32_t value)
{
    (void)context;
    /* This stage permits exactly the discovered CR4 bank selector. */
    if (!cores.cores_valid) { return BLOCK_UNSUPPORTED; }
    for (unsigned i=0;i<cores.core_count;++i) {
        if (cores.core[i].id==0x83e && address==cores.core[i].base+0x40) {
            return brcm_sdio_write32(&card,&chip,address,value);
        }
    }
    return BLOCK_UNSUPPORTED;
}
static void core_status(void)
{
    console_write("Wi-Fi core discovery: "); console_write(block_error(cores.error));
    console_write(cores.cores_valid ? " core_map=valid" : " core_map=unverified");
    console_write(cores.ram_valid ? " RAM=measured\n" : " RAM=unavailable\n");
    console_write("EROM="); console_hex(cores.erom); console_write(" words="); console_u64(cores.rom_words);
    console_write(" cores="); console_u64(cores.core_count); console_write("\n");
    for (unsigned i=0;i<cores.core_count;++i) {
        const struct brcm_core *c=&cores.core[i];
        console_write("core="); console_hex(c->id); console_write(" "); console_write(brcm_core_name(c->id));
        console_write(" rev="); console_u64(c->revision); console_write(" base="); console_hex(c->base);
        console_write(" wrapper="); console_hex(c->wrapper); console_write("\n");
    }
    if (cores.cores_valid) {
        console_write("CR4 discovery snapshot: ioctl="); console_hex(cores.arm_ioctl); console_write(" reset="); console_hex(cores.arm_reset);
        console_write(" capability="); console_hex(cores.arm_cap); console_write(" banks="); console_u64(cores.banks); console_write("\n");
    }
    if (cores.bank_changed) { console_write(cores.bank_restored ? "CR4 bank selector restored and verified.\n" : "CR4 bank selector restoration unconfirmed; reboot before further Wi-Fi work.\n"); }
    if (cores.ram_valid) {
        console_write("Wi-Fi RAM base (chip map)="); console_hex(cores.ram_base); console_write(" bytes="); console_u64(cores.ram_bytes);
        console_write(" KiB="); console_u64(cores.ram_bytes/1024); console_write("\n");
        for (unsigned i=0;i<cores.banks;++i) {
            console_write("bank="); console_u64(i); console_write(" bytes="); console_u64(cores.bank_bytes[i]); console_write("\n");
        }
    }
    if (cores.error!=BLOCK_OK) {
        console_write("last_address="); console_hex(cores.last_address); console_write(" word="); console_hex(cores.last_word); console_write("\n");
        if (!cores.cores_valid) {
            for (unsigned i=0;i<cores.rom_words && i<32;++i) {
                console_write("EROM["); console_u64(i); console_write("]="); console_hex(cores.rom_sample[i]); console_write("\n");
            }
        }
    }
}
static void firmware_status(void)
{
    console_write("Wi-Fi firmware staging: ");console_write(block_error(firmware.error));
    console_write(" phase=");console_u64(firmware.phase);
    console_write(firmware.complete ? " upload/readback=verified\n" : " upload/readback=incomplete\n");
    console_write(firmware.cpu_halted ? "CR4 last-confirmed halted=yes" : "CR4 halt not confirmed");
    console_write(firmware.d11_disabled ? " D11 held in reset=yes\n" : " D11 reset not confirmed\n");
    console_write("code_bytes=");console_u64(firmware.uploaded);console_write(" verified_bytes=");console_u64(firmware.verified);
    console_write(" NVRAM_bytes=");console_u64(firmware.nvram_size);console_write(" NVRAM_address=");console_hex(firmware.nvram_address);console_write("\n");
    if (firmware.token_cleared) { console_write("Initial NVRAM token clear/readback succeeded.\n"); }
    if (firmware.error!=BLOCK_OK) { console_write("last_address=");console_hex(firmware.last_address);console_write("\n"); }
    transfer_failure();
    if (startup.may_run) { console_write("CR4 release attempted; passive upload status is historical, not current CPU/D11 state.\n"); }
    else if (firmware.cpu_halted) { console_write("Firmware is not executing. Next: controlled startup and FullMAC; no Wi-Fi connection.\n"); }
    else { console_write("Firmware execution state unconfirmed; no Wi-Fi connection.\n"); }
}
static enum block_result firmware_write(void *context,uint32_t address,uint32_t value)
{
    (void)context;
    if ((address==0x18101408 && (value==15 || value==7)) || (address==0x18101800 && value==1)) { return brcm_sdio_write32(&card,&chip,address,value); }
    return BLOCK_UNSUPPORTED;
}
static enum block_result firmware_ram(void *context,bool write,uint32_t address,void *data,size_t length)
{
    (void)context;
    if (!firmware.cpu_halted || !firmware.d11_disabled || !firmware.attempted) { return BLOCK_NOT_READY; }
    return brcm_sdio_ram(&card,&chip,write,address,data,length);
}
static enum block_result firmware_delay(void *context,unsigned us)
{
    (void)context;uint64_t hz=timer_frequency();if (!hz || us>10000) { return BLOCK_ARGUMENT; }
    uint64_t start=timer_ticks(),wait=(hz*us+999999)/1000000;
    /* Startup needs 5/10 ms waits as well as the loader's 10 us reset wait.
     * Keep a stuck-counter limit with room for 10 ms on a fast Cortex-A76. */
    for (unsigned spin=0;spin<100000000;++spin) { if (timer_ticks()-start>=wait) { return BLOCK_OK; } }
    return BLOCK_TIMEOUT;
}
static void firmware_progress(void *context,uint32_t verified,uint32_t total)
{ (void)context;console_write("Firmware readback verified ");console_u64(verified);console_write("/");console_u64(total);console_write(" bytes\n"); }
void platform_wifi_load(void)
{
    if (firmware.attempted) { console_write("Wi-Fi firmware staging already attempted; reboot before retry.\n");firmware_status();return; }
    if (!card.selected || !cores.ram_valid || !cores.bank_restored || cores.error!=BLOCK_OK) { console_write("Run wifiinit and wificores successfully before wifiload.\n");return; }
    const unsigned char *mac;uint32_t length;
    if (!firmware_dtb || !dt_property((const void *)firmware_dtb,0x200000,"/axi/mmc@1100000/wifi@1","local-mac-address",&mac,&length) || length!=6) { console_write("Firmware staging requires the board's DT Wi-Fi MAC; no device writes.\n");return; }
    console_write("wifiload: verify files, keep CR4 halted, reset D11, upload and compare every byte.\nWait for the prompt; at 400 kHz SDIO this can take a minute or more.\n");
    struct brcm_fw_io io={NULL,wifi_file_open,wifi_file_read,core_read,firmware_write,firmware_ram,firmware_delay,firmware_progress};
    (void)brcm_fw_upload(&io,chip.chip_id_word,&cores,mac,&firmware);
    wifi_files_report(&firmware);firmware_status();
    if (!card.selected) { host_status(); }
}
static enum block_result start_write(void *context,uint32_t address,uint32_t value)
{
    (void)context;if (!startup.attempted || !firmware.complete) { return BLOCK_NOT_READY; }
    if ((address==0x18004020 && value==UINT32_MAX) ||
        (address==0x18101800 && value==0) || (address==0x18101408 && value==5) ||
        (address==0x18102408 && (value==0x23 || value==3 || value==1)) ||
        (address==0x18102800 && value<=1)) { return brcm_sdio_write32(&card,&chip,address,value); }
    return BLOCK_UNSUPPORTED;
}
static enum block_result start_ram(void *context,uint32_t address,void *data,size_t n)
{ (void)context;return brcm_sdio_ram(&card,&chip,false,address,data,n); }
static enum block_result start_vector(void *context,uint32_t value)
{
    (void)context;
    if (!startup.attempted || startup.phase!=START_VECTOR || !firmware.complete || !firmware.cpu_halted || value!=firmware.reset_vector) { return BLOCK_NOT_READY; }
    return brcm_sdio_reset_vector(&card,&chip,value);
}
static enum block_result start_clock(void *context,bool write,uint8_t *value)
{
    (void)context;
    if (!startup.attempted || startup.phase!=START_CLOCK || !value) { return BLOCK_NOT_READY; }
    if (write) { return *value==0x10 ? sdio_write_byte(&card,1,0x1000e,*value) : BLOCK_UNSUPPORTED; }
    enum block_result r=sdio_read_byte(&card,1,0x1000e,value);if (r==BLOCK_OK) { chip.clock_csr=*value; }return r;
}
static void startup_status(void)
{
    console_write("Wi-Fi firmware startup: ");console_write(block_error(startup.error));
    console_write(" phase=");console_u64(startup.phase);
    console_write(startup.ready ? " shared_memory=ready\n" : " shared_memory=unverified\n");
    console_write(startup.may_run ? "CR4 may be executing; halt is not asserted by this status.\n" : "CR4 release not attempted.\n");
    console_write("reset_vector=");console_hex(startup.reset_vector);console_write(" clock_csr=");console_hex(startup.clock_csr);
    console_write(" shared=");console_hex(startup.shared);console_write(" polls=");console_u64(startup.polls);console_write("\n");
    console_write("shared_flags=");console_hex(startup.flags);console_write(" console=");console_hex(startup.console);
    console_write(" trap=");console_hex(startup.trap);console_write(" assert_line=");console_u64(startup.assert_line);
    console_write(" mailbox=");console_hex(startup.mailbox);console_write("\n");
    if (startup.error!=BLOCK_OK) { console_write("last_address=");console_hex(startup.last_address);console_write(" last_word=");console_hex(startup.last_word);console_write("\n"); }
    if (control.attempted) { console_write("Startup snapshot; see the separate control-channel status.\n"); }
    else { console_write("Function 2 / FullMAC control not started; scan/connect/WPA3/IP unavailable.\n"); }
    if (startup.attempted && !startup.ready) { console_write("Startup incomplete; capture wifiinfo, then reboot before retry.\n"); }
}
void platform_wifi_start(void)
{
    if (startup.attempted) { console_write("Wi-Fi startup already attempted; cached result only. Reboot before retry.\n");startup_status();return; }
    if (!card.selected || !firmware.complete || !firmware.cpu_halted) { console_write("Run storageinit, wifiinit, wificores and wifiload successfully before wifistart.\n");return; }
    console_write("wifistart: verified staging required; install CR4 vector, release CPU, check HT/shared memory.\nNo scan, association or network requests are issued. Wait for the prompt.\n");
    struct brcm_start_io io={NULL,core_read,start_write,start_ram,start_vector,start_clock,firmware_delay};
    (void)brcm_fw_start(&io,chip.chip_id_word,&cores,&firmware,&startup);
    startup_status();transfer_failure();if (!card.selected) { host_status(); }
}
static enum block_result control_read8(void *ctx,unsigned fn,uint32_t address,uint8_t *value)
{
    (void)ctx;enum block_result r=sdio_read_byte(&card,fn,address,value);
    if (r==BLOCK_OK && fn==1 && address==0x1000e) { chip.clock_csr=*value; }return r;
}
static enum block_result control_write8(void *ctx,unsigned fn,uint32_t address,uint8_t value)
{
    (void)ctx;if (!control.attempted || !startup.ready) { return BLOCK_NOT_READY; }
    if ((!fn && ((address==0x210 && value==0) || (address==0x211 && value==2) ||
        (address==2 && value==(uint8_t)(control.enable|4)))) ||
        (fn==1 && ((address==0x1000e && value==0x12) || (address==0x10008 && value==8)))) {
        return sdio_write_byte(&card,fn,address,value);
    }return BLOCK_UNSUPPORTED;
}
static enum block_result control_write32(void *ctx,uint32_t address,uint32_t value)
{
    (void)ctx;if (!control.attempted || !startup.ready) { return BLOCK_NOT_READY; }
    if ((address==0x18004048 && value==0x40000) || (address==0x18004024 && value==0xf0) ||
        (address==0x18004040 && value==2) || (address==0x18004020 && value && !(value&~0xf0u))) {
        return brcm_sdio_write32(&card,&chip,address,value);
    }return BLOCK_UNSUPPORTED;
}
static enum block_result control_packet(void *ctx,bool write,void *data,size_t n)
{
    (void)ctx;if (!startup.ready || !control.attempted || !control.f2_ready) { return BLOCK_NOT_READY; }
    return brcm_sdio_packet(&card,&chip,write,data,n);
}
static void control_status(void)
{
    console_write("Wi-Fi control channel: ");console_write(block_error(control.error));console_write(" phase=");console_u64(control.phase);
    console_write(control.ready ? " queries=verified\n" : " queries=incomplete\n");
    console_write("F2 block_size=");console_u64(control.block_size);console_write(" IO_ENABLE=");console_hex(control.enable);
    console_write(" IO_READY=");console_hex(control.io_ready);console_write(" clock_csr=");console_hex(control.clock_csr);
    console_write(" device_ready=");console_write(control.device_ready ? "yes\n" : "no\n");
    console_write("interrupt=");console_hex(control.interrupt);console_write(" mailbox=");console_hex(control.mailbox);
    console_write(" last_address=");console_hex(control.last_address);console_write(" last_word=");console_hex(control.last_word);console_write("\n");
    console_write("TX_frames=");console_u64(control.tx_frames);console_write(" RX_frames=");console_u64(control.rx_frames);
    console_write(" events=");console_u64(control.event_frames);console_write(" data_dropped=");console_u64(control.data_frames);
    console_write(" empty=");console_u64(control.empty_frames);console_write(" sequence_resync=");console_u64(control.seq_resync);
    console_write(" polls=");console_u64(control.polls);console_write("\n");
    console_write("Last RX header:");for (unsigned i=0;i<3;++i) { console_write(" ");console_hex(control.last_header[i]); }console_write("\n");
    console_write("tx_seq=");console_u64(control.tx_seq);console_write(" tx_window=");console_u64(control.tx_max);
    console_write(" request_id=");console_u64(control.request_id);console_write(" command=");console_u64(control.command);
    console_write(" firmware_status=");console_hex(control.firmware_status);console_write(" response_bytes=");console_u64(control.response_length);console_write("\n");
    if (control.ioctl_version) { console_write("Firmware ioctl ABI=");console_u64(control.ioctl_version);console_write("\n"); }
    if (control.ready) { console_write("Firmware version: ");console_write(control.version);console_write("\n"); }
    console_write("Version queries snapshot; preparation/scan have separate status. No association/WPA3/IP.\n");
    if (control.attempted && !control.ready) { console_write("Control startup incomplete; capture wifiinfo, then reboot before retry.\n"); }
}
void platform_wifi_control(void)
{
    if (control.attempted) { console_write("Wi-Fi control already attempted; cached result only. Reboot before retry.\n");control_status();return; }
    if (!card.selected || !startup.ready || startup.error!=BLOCK_OK) { console_write("Run wifistart successfully before wificontrol.\n");return; }
    console_write("wificontrol: enable F2, check protocol/credits, query ioctl and firmware versions.\nNo scan, association, credentials or IP requests. Wait for the prompt.\n");
    struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    (void)brcm_control_start(&io,&startup,&control);control_status();transfer_failure();if (!card.selected) { host_status(); }
}
static enum block_result prepare_iovar(void *ctx,bool set,const char *name,const void *input,size_t n,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_iovar(&io,&control,set,name,input,n,output,capacity,actual,status);
}
static void caps_status(void)
{
    console_write("Wi-Fi capabilities: ");console_write(block_error(capabilities.error));console_write(" phase=");console_u64(capabilities.phase);
    console_write(capabilities.ready ? " discovery=verified\n" : " discovery=incomplete\n");
    if (capabilities.ready) {
        console_write("Firmware cap: ");console_write(capabilities.caps);console_write("\nFirmware MAC: ");
        static const char hex[]="0123456789abcdef";
        for (unsigned i=0;i<6;++i) { if (i) { console_putc(':'); }console_putc(hex[capabilities.mac[i]>>4]);console_putc(hex[capabilities.mac[i]&15]); }console_write("\n");
        console_write(capabilities.sae ? "SAE advertised=yes" : "SAE advertised=no");
        console_write(capabilities.external_sae ? " external_SAE advertised=yes\n" : " external_SAE advertised=no\n");
        console_write(capabilities.mfp_supported ? "PMF iovar replied=yes current_value=" : capabilities.mfp_known ? "PMF iovar unsupported current_value=" : "PMF support unconfirmed current_value=");console_u64(capabilities.mfp_value);
        console_write(" firmware_status=");console_hex(capabilities.mfp_status);console_write("\n");
        console_write(capabilities.supplicant_supported ? "Firmware supplicant iovar replied=yes current_value=" : capabilities.supplicant_known ? "Firmware supplicant iovar unsupported current_value=" : "Firmware supplicant support unconfirmed current_value=");console_u64(capabilities.supplicant_value);
        console_write(" firmware_status=");console_hex(capabilities.supplicant_status);console_write("\n");
        console_write("Capability discovery does not enable SAE/PMF or prove a WPA3 connection.\n");
    }
    console_write("last_firmware_status=");console_hex(capabilities.firmware_status);console_write("\n");
}
static void prepare_status(void)
{
    console_write("Wi-Fi regulatory preparation: ");console_write(block_error(preparation.error));console_write(" phase=");console_u64(preparation.phase);
    console_write(preparation.ready ? " CLM/country=verified\n" : " CLM/country=incomplete\n");
    console_write("CLM pin=");console_write(preparation.files_verified ? "verified" : "unverified");
    console_write(" uploaded_bytes=");console_u64(preparation.uploaded);console_write(" chunks=");console_u64(preparation.chunks);
    console_write(" clmload_status=");if (preparation.clm_status_valid) { console_hex(preparation.clm_status); }else { console_write("unread"); }
    console_write(" firmware_status=");console_hex(preparation.firmware_status);console_write("\n");
    if (preparation.clm_complete) { console_write("CLM version: ");console_write(preparation.clm_version);console_write("\n"); }
    console_write("Requested country=");console_write(preparation.requested);console_write(" verified country=");console_write(preparation.country);
    console_write(" ccode=");console_write(preparation.ccode);console_write(" revision=");console_u64(preparation.country_revision);console_write("\n");
    console_write("Country raw reply:");for (unsigned i=0;i<12;++i) { console_write(" ");console_hex(preparation.country_reply[i]); }console_write("\n");
    console_write("Preparation snapshot; wifiscan is separate. Association/WPA3/IP remain unimplemented.\n");
    if (preparation.attempted && !preparation.ready) { console_write("Partial preparation; capture wifiinfo, then reboot before retry.\n"); }
}
void platform_wifi_caps(void)
{
    if (capabilities.attempted) { console_write("Wi-Fi capability discovery already attempted; cached result only.\n");caps_status();return; }
    if (!card.selected || !control.ready || control.error!=BLOCK_OK) { console_write("Run wificontrol successfully before wificaps.\n");return; }
    struct brcm_prepare_io io={NULL,prepare_iovar,wifi_file_open,wifi_file_read};
    (void)brcm_caps_read(&io,&capabilities);caps_status();transfer_failure();
}
void platform_wifi_prepare(const char *country)
{
    if (!brcm_country_valid(country)) { console_write("Usage: wifiprepare IN (use your actual two-letter uppercase country code).\n");return; }
    if (preparation.attempted) { console_write("Wi-Fi preparation already attempted; cached result only. Reboot before changing country/retrying.\n");prepare_status();return; }
    if (!card.selected || !control.ready || !capabilities.ready || control.error!=BLOCK_OK) { console_write("Run wificontrol and wificaps successfully before wifiprepare.\n");return; }
    console_write("Verifying pinned CLM before upload; then setting and reading back country. No scan/association.\n");
    struct brcm_prepare_io io={NULL,prepare_iovar,wifi_file_open,wifi_file_read};
    (void)brcm_wifi_prepare(&io,country,&preparation);prepare_status();transfer_failure();
}
static enum block_result scan_radio(void *ctx,bool up,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_radio(&io,&control,up,status);
}
static enum block_result scan_poll(void *ctx)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_poll(&io,&control);
}
static uint64_t scan_milliseconds(void *ctx)
{ (void)ctx;uint64_t hz=timer_frequency();return hz ? timer_ticks()*1000/hz : 0; }
static void print_mac(const unsigned char *mac)
{
    static const char hex[]="0123456789abcdef";
    for (unsigned i=0;i<6;++i) { if (i) { console_putc(':'); }console_putc(hex[mac[i]>>4]);console_putc(hex[mac[i]&15]); }
}
static void print_ssid(const struct brcm_scan_ap *ap)
{
    if (!ap->ssid_length) { console_write("<hidden>");return; }
    static const char hex[]="0123456789ABCDEF";console_putc('"');
    for (unsigned i=0;i<ap->ssid_length;++i) {
        unsigned c=ap->ssid[i];if (c>=32 && c<=126 && c!='"' && c!='\\') { console_putc((char)c); }
        else { console_write("\\x");console_putc(hex[c>>4]);console_putc(hex[c&15]); }
    }console_putc('"');
}
static void scan_status(void)
{
    console_write("Wi-Fi passive scan: ");console_write(block_error(scan.error));console_write(" phase=");console_u64(scan.phase);
    console_write(scan.ready ? " scan=complete\n" : " scan=incomplete\n");
    console_write("escan_version=");console_u64(scan.escan_version);console_write(" scan_ver_reply=");console_hex(scan.scan_version);
    console_write(" sync_id=");console_hex(scan.sync_id);console_write(" firmware_status=");console_hex(scan.firmware_status);
    console_write(" event_status=");console_u64(scan.event_status);console_write(" reason=");console_u64(scan.event_reason);console_write("\n");
    console_write("networks=");console_u64(scan.count);console_write(" events=");console_u64(scan.events);console_write(" duplicates=");console_u64(scan.duplicates);
    console_write(" malformed=");console_u64(scan.malformed);console_write(" ignored=");console_u64(scan.ignored);console_write(" overflow=");console_u64(scan.overflow);console_write(" polls=");console_u64(scan.polls);console_write("\n");
    console_write(scan.interface_down ? "Interface DOWN acknowledged. " : scan.interface_may_be_up ? "Interface state may be UP; reboot after failure. " : "Interface UP not attempted. ");
    console_write(scan.mask_restored ? "Event mask restored and verified.\n" : "Event mask restoration unconfirmed.\n");
    for (unsigned i=0;i<scan.count;++i) {
        const struct brcm_scan_ap *ap=&scan.ap[i];print_mac(ap->bssid);console_write(" channel=");console_u64(ap->channel);console_write(" band=");console_u64(ap->band);console_write("GHz RSSI=");
        if (ap->rssi==-128) { console_write("unknown"); }else { if (ap->rssi<0) { console_putc('-'); }console_u64((uint64_t)(ap->rssi<0 ? -ap->rssi : ap->rssi));console_write("dBm"); }
        console_write(" security=");
        if (ap->security&SCAN_RSN) {
            console_write("RSN");if (ap->security&SCAN_PSK) { console_write("+PSK"); }if (ap->security&SCAN_SAE) { console_write("+SAE"); }
            if (ap->security&SCAN_OWE) { console_write("+OWE"); }if (ap->security&SCAN_ENTERPRISE) { console_write("+802.1X"); }if (ap->security&SCAN_OTHER_AKM) { console_write("+OTHER"); }
        }else if (ap->security&SCAN_WPA) { console_write("WPA"); }else if (ap->security&SCAN_PRIVACY) { console_write("PRIVACY"); }else { console_write("OPEN"); }
        console_write(ap->pmf_required ? " PMF=required SSID=" : ap->pmf_capable ? " PMF=capable SSID=" : " PMF=not-advertised SSID=");print_ssid(ap);console_write("\n");
    }
    if (scan.truncated) { console_write("Result table full; additional networks were counted but not stored.\n"); }
    if (scan.malformed) { console_write("last_BSS_version=");console_u64(scan.last_bss_version);console_write(" last_parse_error=");console_write(block_error(scan.last_parse_error));console_write("\n"); }
    console_write("Network advertisements only; not associated. WPA3 authentication and IP are unavailable.\n");
    if (scan.attempted && !scan.ready) { console_write("Capture wifiinfo, then reboot before retrying the scan.\n"); }
}
void platform_wifi_scan(void)
{
    if (scan.attempted) { console_write("Wi-Fi scan already attempted; cached results only. Reboot for a fresh scan.\n");scan_status();return; }
    if (!card.selected || !control.ready || !preparation.ready || control.ioctl_version!=2) { console_write("Run wificontrol, wificaps and wifiprepare IN successfully before wifiscan; ioctl ABI 2 required.\n");return; }
    console_write("wifiscan: passive scan after verified CLM/country; no association or credentials.\nAllow up to 30 seconds plus I/O; wait for the prompt.\n");
    control.event=brcm_scan_event;control.event_context=&scan;control.rx_limit=control.rx_frames+1024;
    struct brcm_scan_io io={NULL,prepare_iovar,scan_radio,scan_poll,firmware_delay,scan_milliseconds};
    (void)brcm_scan_run(&io,&preparation,&scan);
    control.event=NULL;control.event_context=NULL;control.rx_limit=control.rx_frames+64;
    scan_status();transfer_failure();if (!card.selected) { host_status(); }
}
void platform_wifi_target(const char *ssid)
{
    size_t bytes=0;while (ssid && ssid[bytes] && bytes<=32) { ++bytes; }
    const struct brcm_scan_ap *ap=NULL;
    enum wifi_target_result r=wifi_target_check(&scan,&capabilities,ssid,bytes,&ap);
    console_write("WPA3 target check: ");console_write(wifi_target_error(r));console_write("\n");
    if (ap) {
        console_write("SSID=");print_ssid(ap);console_write(" BSSID=");print_mac(ap->bssid);
        console_write(" channel=");console_u64(ap->channel);console_write(" band=");console_u64(ap->band);console_write("GHz\n");
        console_write("RSN group=");console_hex(ap->group_cipher);console_write(" pairwise_CCMP128=");console_u64(ap->pairwise_ccmp);
        console_write(" SAE_AKM8=");console_u64(ap->akm_sae);console_write(" management=");console_hex(ap->management_cipher);console_write("\n");
        console_write(ap->pmf_required ? "AP PMF=required; station policy=required.\n" : ap->pmf_capable ? "AP PMF=capable; station policy=required.\n" : "AP PMF=not-advertised; station policy=required.\n");
        console_write("RSNXE=");
        if (!ap->rsnxe_length) { console_write("absent; SAE password-element mode has not been negotiated.\n"); }
        else {
            for (unsigned i=0;i<ap->rsnxe_length;++i) { console_hex(ap->rsnxe[i]);console_putc(' '); }
            console_write((ap->rsnxe[0]&32) ? "SAE H2E advertised.\n" : "SAE H2E not advertised.\n");
        }
    }
    if (r==WIFI_TARGET_ARGUMENT) { console_write("Usage: wifitarget <exact SSID> (no surrounding quotes; spaces/apostrophes are literal).\n"); }
    console_write("Cached advertisements only. No password accepted or packets sent; not authenticated or connected.\n");
}
static void auth_probe_status(void)
{
    static const char *const properties[]={"none","auth","wpa_auth","wsec","mfp","event_msgs_ext","event_msgs"};
    console_write("Firmware auth-policy check: ");console_write(block_error(auth_probe.error));console_write(" phase=");console_u64(auth_probe.phase);
    console_write(" operation=");console_write(auth_probe.property<7 ? properties[auth_probe.property] : "unknown");console_write("\n");
    console_write(auth_probe.verified ? "Temporary SAE/AES/required-PMF and extended events verified.\n" : "Temporary policy/event verification incomplete.\n");
    console_write("event_mask_bytes=");console_u64(auth_probe.mask_bytes);console_write(" external-auth event IDs=187,188,189,190\n");
    console_write("Extended mask reply_bytes=");console_u64(auth_probe.mask_reply_bytes);
    console_write(" version=");console_u64(auth_probe.mask_header[0]);console_write(" command=");console_u64(auth_probe.mask_header[1]);
    console_write(" length=");console_u64(auth_probe.mask_header[2]);console_write(" maxgetsize=");console_u64(auth_probe.mask_header[3]);console_write("\n");
    console_write("Mask header compatibility=");console_write(auth_probe.mask_compatibility ? "captured ev/24/24" : "v1 or rejected");
    console_write(" legacy_crosscheck=");console_write(auth_probe.mask_legacy_verified ? "verified" : "not-verified");
    console_write(" initial_snapshot=");console_write(auth_probe.mask_snapshot_stable ? "stable" : "not-verified");console_write("\n");
    for (unsigned i=0;i<4;++i) {
        console_write(properties[i+1]);console_putc('=');
        if (auth_probe.observed_mask&(1u<<i)) { console_hex(auth_probe.observed[i]); }else { console_write("not-read"); }console_putc(' ');
    }console_write("\n");
    console_write("firmware_status=");console_hex(auth_probe.firmware_status);console_write(" restore_status=");console_hex(auth_probe.restore_status);console_write("\n");
    if (!auth_probe.changed) { console_write("No firmware settings changed.\n"); }
    else if (auth_probe.restored) { console_write("Original security policy and complete event mask restored and verified.\n"); }
    else { console_write("RESTORATION UNCONFIRMED: reboot before further Wi-Fi work.\n"); }
    console_write("Interface remains DOWN. No join, credential, management-frame send or network connection.\n");
}
void platform_wifi_auth_prepare(const char *ssid)
{
    if (auth_probe.attempted) { console_write("Firmware auth-policy check already attempted; cached status. Reboot to repeat.\n");auth_probe_status();return; }
    size_t n=0;while (ssid && ssid[n] && n<=32) { ++n; }const struct brcm_scan_ap *ap=NULL;
    enum wifi_target_result target=wifi_target_check(&scan,&capabilities,ssid,n,&ap);
    if (target!=WIFI_TARGET_OK) { console_write("Auth preparation refused: ");console_write(wifi_target_error(target));console_write("\nUsage: wifiauthprep <exact SSID> after a successful scan.\n");return; }
    if (!ap->rsnxe_length || !(ap->rsnxe[0]&32)) { console_write("Target does not advertise H2E; this backend requires H2E. No firmware settings changed.\n");return; }
    if (!card.selected || !control.ready || control.error!=BLOCK_OK || control.ioctl_version!=2 || !preparation.ready) { console_write("Run the firmware/control/country sequence successfully before auth preparation.\n");return; }
    console_write("Checking firmware SAE/AES/required-PMF and extended auth-event mask, then restoring.\nTarget=");print_ssid(ap);console_write(" BSSID=");print_mac(ap->bssid);console_write("\n");
    const struct brcm_auth_probe_io io={NULL,prepare_iovar};(void)brcm_auth_probe_run(&io,true,&auth_probe);
    if (auth_probe.changed && !auth_probe.restored) { control.ready=false;control.error=auth_probe.restore_error==BLOCK_OK ? BLOCK_IO : auth_probe.restore_error; }
    auth_probe_status();transfer_failure();
}
static uint32_t rd(void *context,unsigned offset)
{
    (void)context; uint32_t value=*(volatile uint32_t *)(WIFI_HOST+offset);
    __asm__ volatile("dmb sy" ::: "memory"); return value;
}
static void wr(void *context,unsigned offset,uint32_t value)
{
    (void)context; __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)(WIFI_HOST+offset)=value; __asm__ volatile("dsb sy" ::: "memory");
}
static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }
static bool power_rd(void *context,uint64_t address,uint32_t *value)
{
    (void)context; *value=*(volatile uint32_t *)(uintptr_t)address;
    __asm__ volatile("dmb sy" ::: "memory"); return true;
}
static bool power_wr(void *context,uint64_t address,uint32_t value)
{
    (void)context; __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)address=value; __asm__ volatile("dsb sy" ::: "memory"); return true;
}
static void host_status(void)
{
    console_write("Wi-Fi SDIO host="); console_hex(WIFI_HOST);
    console_write(" CONTROL0="); console_hex(rd(NULL,0x28)); console_write(" CONTROL1="); console_hex(rd(NULL,0x2c));
    console_write("\nPRESENT="); console_hex(rd(NULL,0x24)); console_write(" CAPS="); console_hex(rd(NULL,0x40));
    console_write(" CQE="); console_hex(rd(NULL,0x208)); console_write("\n");
    console_write("Current host IRQ=");console_hex(rd(NULL,0x30));console_write("\n");
}
void platform_wifi_status(void)
{
    console_write("Board power phase="); console_u64(power.phase); console_write(" result="); console_write(block_error(power.error));
    console_write(" stepping="); console_hex(power.stepping); console_write(" SD_clock_hz="); console_u64(power.sd_hz); console_write("\n");
    console_write("SDIO init phase="); console_u64(card.phase); console_write(" result="); console_write(block_error(card.error));
    console_write(card.selected ? " selected=yes" : " selected=no"); console_write("\n");
    if (card.changed) {
        console_write("R4="); console_hex(card.r4); console_write(" RCA="); console_hex(card.rca);
        console_write(" functions="); console_u64(card.functions); console_write(" CCCR="); console_u64(card.cccr_revision);
        console_write(" SDIO_revision="); console_u64(card.sdio_revision); console_write("\nlast_irq="); console_hex(card.last_interrupt);
        console_write(" last_response="); console_hex(card.last_response); console_write("\n");
    }
    if (card.have_identity) {
        console_write("SDIO CIS vendor="); console_hex(card.vendor); console_write(" device="); console_hex(card.device);
        console_write(" (SDIO identity; not backplane chip/revision)\n");
    }
    console_write("Broadcom phase="); console_u64(chip.phase); console_write(" result="); console_write(block_error(chip.error));
    console_write(" clock_csr="); console_hex(chip.clock_csr); console_write("\n");
    console_write("Function-1 block size=");console_u64(chip.function1_block_size);console_write(" bytes; RAM wire requests <=64 bytes\n");
    if (chip.chip_read) {
        console_write("ChipCommon ID word="); console_hex(chip.chip_id_word);
        console_write(" chip="); console_hex(chip.chip_id_word&0xffff);
        console_write(" revision="); console_u64((chip.chip_id_word>>16)&15); console_write("\n");
    }
    if (cores.attempted) { core_status(); }
    if (firmware.phase!=FW_IDLE) { firmware_status(); }
    else { transfer_failure(); }
    if (startup.phase!=START_IDLE) { startup_status(); }
    if (control.phase!=CONTROL_IDLE) { control_status(); }
    if (capabilities.attempted) { caps_status(); }
    if (preparation.phase) { prepare_status(); }
    if (scan.phase) { scan_status(); }
    if (auth_probe.attempted) { auth_probe_status(); }
}
void platform_wifi_cores(void)
{
    if (cores.attempted) { console_write("Wi-Fi core discovery already attempted; cached result follows. Reboot before retry.\n"); core_status(); return; }
    if (!card.selected || !chip.chip_read || chip.error!=BLOCK_OK) { console_write("Run wifiinit successfully before wificores.\n"); return; }
    console_write("Reading AI core table and CR4 RAM banks; no CPU/D11 reset, firmware upload or RF packets.\n");
    struct brcm_chip_io io={core_read,core_write,NULL};
    (void)brcm_chip_discover(&io,chip.chip_id_word,&cores); core_status();
    if (!card.selected) { host_status(); }
}
void platform_wifi_init(uintptr_t dtb)
{
    firmware_dtb=dtb;
    if (card.changed || power.attempted) { console_write("Wi-Fi SDIO init already attempted; reboot before retry.\n"); platform_wifi_status(); return; }
    console_write("Wi-Fi init: SDIO enumeration only; no RF association or network packets.\n");
    if ((rd(NULL,0x2c)&7)!=7 || !(rd(NULL,0x28)&256)) {
        console_write("Cold Wi-Fi host: preparing WLAN power, pins and 400 kHz clock.\n");
        struct wifi_power_io board_io={power_rd,power_wr,ticks,NULL,timer_frequency()};
        enum block_result prepared=pi5_wifi_power_up(&board_io,(const void *)dtb,0x200000,&power);
        console_write("Board preparation: "); console_write(block_error(prepared)); console_write("\n");
        if (prepared!=BLOCK_OK) { host_status(); platform_wifi_status(); return; }
    }
    struct sdhci_io io={rd,wr,ticks,NULL,timer_frequency()};
    enum block_result r=sdio_initialize(&card,&io);
    console_write("SDIO initialization: "); console_write(block_error(r)); console_write("\n");
    if (r==BLOCK_OK) {
        /* Initialization already parsed the CIS via CMD52. Avoid reading past
         * its end or issuing an unsupported optional function-0 CMD53 probe. */
        r=brcm_sdio_identify(&card,&chip); console_write("Function-1 backplane identify: ");
        console_write(block_error(r)); console_write("\n");
    }
    host_status(); platform_wifi_status();
    console_write("WPA3-Personal requested; not connected. Remaining: core discovery/upload,\n");
    console_write("board firmware, FullMAC, SAE/PMF authentication and an IP stack.\n");
}
void platform_wifi_probe(void)
{
    host_status();
    console_write("Wi-Fi SDIO host="); console_hex(WIFI_HOST);
    console_write(" clock_control="); console_hex(rd(NULL,0x2c)); console_write("\n");
    struct sdhci_io io={rd,wr,ticks,NULL,timer_frequency()}; uint32_t response=0;
    enum block_result r=sdio_query(&io,&response);
    console_write("SDIO CMD5 query: "); console_write(block_error(r));
    if (r==BLOCK_OK) {
        console_write(" R4="); console_hex(response);
        console_write(" functions="); console_u64((response>>28)&7);
        console_write(" ready="); console_u64(response>>31);
    }
    console_write("\nQuery only. wifiinit attempts SDIO enumeration on a powered legacy host; no connection yet.\n");
}
