/* Manual SDIO bring-up, verified staging and explicit firmware startup.
 * Never called at boot. The upload keeps CR4 halted; wifistart releases it.
 * F2 control supports bounded preparation/scan/request probes; no IP interface. */
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
#include "brcm_join_probe.h"
#include "brcm_sae_probe.h"
#include "brcm_assoc.h"
#include "sae_h2e.h"
#include "sae_hunt.h"
#include "sae_session.h"
#include "rng200.h"
#include "crypto.h"
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
static struct brcm_join_probe join_probe={.error=BLOCK_NOT_READY};
static struct brcm_sae_probe sae_probe={.error=BLOCK_NOT_READY};
static struct brcm_assoc_probe association_probe={.error=BLOCK_NOT_READY};
static bool sae_preparation_attempted;
static uintptr_t firmware_dtb;
static void host_status(void);
static void startup_status(void);
static void control_status(void);
static void prepare_status(void);
static void caps_status(void);
static void scan_status(void);
static void auth_probe_status(void);
static void join_probe_status(void);
static void sae_status(const struct brcm_sae_probe *s);
static void assoc_status(void);
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
    console_write("SDPCM glom: descriptors=");console_u64(control.glom_descriptors);console_write(" aggregates=");console_u64(control.glom_frames);
    console_write(" subframes=");console_u64(control.glom_subframes);console_write(" failures=");console_u64(control.glom_failures);
    console_write(" phase=");console_u64(control.glom_phase);console_write(" result=");console_write(block_error(control.glom_error));console_write("\n");
    if (control.glom_phase) {
        console_write("Glom entries=");console_u64(control.glom_last_count);console_write(" pending=");console_u64(control.glom_count);console_write(" described_bytes=");console_u64(control.glom_bytes);
        console_write(" wire_bytes=");console_u64(control.glom_wire_bytes);console_write(" outer_header:");
        for (unsigned i=0;i<3;++i) { console_write(" ");console_hex(control.glom_header[i]); }console_write("\n");
        console_write("Glom last_index=");console_u64(control.glom_bad_index);console_write(" lengths:");
        for (unsigned i=0;i<control.glom_last_count && i<BRCM_GLOM_FRAMES;++i) { console_write(" ");console_u64(control.glom_lengths[i]); }
        console_write(" subheader:");for (unsigned i=0;i<3;++i) { console_write(" ");console_hex(control.glom_subheader[i]); }console_write("\n");
    }
    console_write("tx_seq=");console_u64(control.tx_seq);console_write(" tx_window=");console_u64(control.tx_max);
    console_write(" request_id=");console_u64(control.request_id);console_write(" command=");console_u64(control.command);
    console_write(" firmware_status=");console_hex(control.firmware_status);console_write(" response_bytes=");console_u64(control.response_length);console_write("\n");
    if (control.ioctl_version) { console_write("Firmware ioctl ABI=");console_u64(control.ioctl_version);console_write("\n"); }
    if (control.ready) { console_write("Firmware version: ");console_write(control.version);console_write("\n"); }
    console_write("Version queries snapshot; preparation/scan/association have separate status. No authorized data port or IP.\n");
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
    console_write("Preparation snapshot; wifiscan is separate. wifiassoc tests key installation/live M4, then DOWN/delete; no persistent link/IP.\n");
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
    console_write("Scan advertisements only; use a separate WPA3 handshake/network test. The scan ends DOWN.\n");
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
        if (!ap->rsnxe_length) { console_write("absent; SAE hunting-and-pecking will be selected (not WPA2).\n"); }
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
    console_write("Firmware auth-policy check: ");console_write(auth_probe.mismatch_mask ? "policy readback mismatch" : block_error(auth_probe.error));console_write(" phase=");console_u64(auth_probe.phase);
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
    console_write("Expected: auth=3 wpa_auth=0x40000 wsec=AES(4 or 0x604 with PMF) mfp=2; mismatch_bits=");console_hex(auth_probe.mismatch_mask);console_write("\n");
    static const unsigned trace_order[]={1,0,2,3};
    for (unsigned step=0;step<4;++step) {
        unsigned i=trace_order[step];console_write("auth_after_");console_write(properties[i+1]);console_putc('=');
        if (auth_probe.auth_trace_mask&(1u<<i)) { console_hex(auth_probe.auth_after[i]); }else { console_write("not-read"); }console_putc(' ');
    }console_write("\n");
    console_write("Temporary extended-event mask readback=");console_write(auth_probe.mask_readback_verified ? "verified" : "not-verified");console_write("\n");
    console_write("firmware_status=");console_hex(auth_probe.firmware_status);console_write(" restore_status=");console_hex(auth_probe.restore_status);console_write("\n");
    if (auth_probe.restore_error!=BLOCK_OK) {
        console_write("First restoration failure: ");console_write(auth_probe.restore_property<7 ? properties[auth_probe.restore_property] : "unknown");
        console_write(" result=");console_write(block_error(auth_probe.restore_error));console_write("\n");
    }
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
    if (wifi_target_sae_method(ap)==WIFI_SAE_METHOD_INVALID) { console_write("Target SAE method advertisement is invalid. No firmware settings changed.\n");return; }
    if (!card.selected || !control.ready || control.error!=BLOCK_OK || control.ioctl_version!=2 || !preparation.ready) { console_write("Run the firmware/control/country sequence successfully before auth preparation.\n");return; }
    console_write("Checking firmware SAE/AES/required-PMF and extended auth-event mask, then restoring.\nTarget=");print_ssid(ap);console_write(" BSSID=");print_mac(ap->bssid);console_write("\n");
    const struct brcm_auth_probe_io io={NULL,prepare_iovar};(void)brcm_auth_probe_run(&io,true,&auth_probe);
    if (auth_probe.changed && !auth_probe.restored) { control.ready=false;control.error=auth_probe.restore_error==BLOCK_OK ? BLOCK_IO : auth_probe.restore_error; }
    auth_probe_status();transfer_failure();
}
static enum block_result join_station(void *ctx,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_station_prepare(&io,&control,status);
}
static enum block_result join_station_live(void *ctx,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_station(&io,&control,status);
}
static void join_poison(void *ctx,enum block_result error)
{ (void)ctx;control.ready=false;control.error=error==BLOCK_OK ? BLOCK_IO : error; }
static void join_probe_status(void)
{
    console_write("External-SAE join probe: ");console_write(block_error(join_probe.error));console_write(" phase=");console_u64(join_probe.phase);console_write("\n");
    console_write("Firmware station mode=");if (control.station_mode_known) { console_u64(control.station_mode); }else { console_write("not-read"); }console_write(" (requires 1)\n");
    console_write("Station setup: phase=");console_u64(control.station_prepare_phase);console_write(" initial=");
    if (control.station_initial_known) { console_u64(control.station_initial_mode); }else { console_write("not-read"); }
    console_write(" AP=");if (control.station_ap_known) { console_u64(control.station_ap_mode); }else { console_write("not-read"); }
    console_write(" UP=");if (control.station_up_known) { console_u64(control.station_up_state); }else { console_write("not-read"); }
    console_write(" SET_INFRA_attempted=");console_write(control.station_change_attempted ? "yes" : "no");
    console_write(" selected=");console_write(control.station_mode_changed ? "verified" : "not-changed");console_write("\n");
    console_write("request_seen=");console_write(join_probe.request_seen ? "yes" : "no");console_write(" flags=");console_hex(join_probe.request_flags);
    console_write(" events=");console_u64(join_probe.events);console_write(" ignored=");console_u64(join_probe.ignored);console_write(" malformed=");console_u64(join_probe.malformed);console_write(" polls=");console_u64(join_probe.polls);console_write("\n");
    console_write("Probe policy result=");console_write(block_error(join_probe.policy.error));console_write(" phase=");console_u64(join_probe.policy.phase);
    console_write(" operation=");console_u64(join_probe.policy.property);console_write(" mismatch_bits=");console_hex(join_probe.policy.mismatch_mask);console_write("\n");
    console_write("Raw auth=");if (join_probe.policy.observed_mask&1) { console_hex(join_probe.policy.observed[0]); }else { console_write("not-read"); }
    console_write(" raw wsec=");if (join_probe.policy.observed_mask&4) { console_hex(join_probe.policy.observed[2]); }else { console_write("not-read"); }
    console_write(" external_request_verified=");console_write(join_probe.policy.external_request_verified ? "yes" : "no");console_write("\n");
    static const char *const names[]={"auth","wpa_auth","wsec","mfp"};
    console_write("After UP: ");for (unsigned i=0;i<4;++i) {
        console_write(names[i]);console_putc('=');if (join_probe.live_read_mask&(1u<<i)) { console_hex(join_probe.live_policy[i]); }else { console_write("not-read"); }console_putc(' ');
    }console_write("event_mask=");console_write(join_probe.live_mask_verified ? "verified" : "not-verified");console_write("\n");
    console_write("Interface DOWN=");console_write(join_probe.down ? "acknowledged" : join_probe.up_attempted ? "UNCONFIRMED" : "not-raised");
    console_write(" policy/mask restoration=");console_write(join_probe.policy.restored ? "verified" : join_probe.policy.changed ? "UNCONFIRMED" : "not-needed");console_write("\n");
    console_write("firmware_status=");console_hex(join_probe.firmware_status);console_write(" cleanup_status=");console_hex(join_probe.cleanup_status);
    console_write(" policy_status=");console_hex(join_probe.policy.firmware_status);console_write(" restore_status=");console_hex(join_probe.policy.restore_status);console_write("\n");
    console_write("Station selection is retained for this boot. Request observation only; no password, SAE response, success notification, keys or IP. Reboot before another join attempt.\n");
}
void platform_wifi_join_probe(const char *ssid)
{
    if (sae_preparation_attempted) { console_write("SAE preparation/probe already used this boot; reboot before another join.\n");return; }
    if (join_probe.attempted) { console_write("Join probe already attempted; cached status only. Reboot to repeat.\n");join_probe_status();return; }
    size_t n=0;while (ssid && ssid[n] && n<=32) { ++n; }const struct brcm_scan_ap *ap=NULL;
    enum wifi_target_result target=wifi_target_check(&scan,&capabilities,ssid,n,&ap);
    if (target!=WIFI_TARGET_OK) { console_write("Join probe refused: ");console_write(wifi_target_error(target));console_write("\n");return; }
    if (wifi_target_sae_method(ap)==WIFI_SAE_METHOD_INVALID || !capabilities.external_sae || !card.selected || !control.ready || control.error!=BLOCK_OK || control.ioctl_version!=2 || !preparation.ready) {
        console_write("Join probe requires clean scan/target, a valid SAE method, external-SAE capability and prepared live control.\n");return;
    }
    struct brcm_auth_peer peer={.ssid_length=ap->ssid_length,.channel=ap->channel,.band=ap->band,.hunting=wifi_target_sae_method(ap)==WIFI_SAE_HUNT};
    for (unsigned i=0;i<6;++i) { peer.local[i]=capabilities.mac[i];peer.peer[i]=ap->bssid[i]; }
    for (unsigned i=0;i<ap->ssid_length;++i) { peer.ssid[i]=ap->ssid[i]; }
    console_write("One-shot join probe: temporary SAE-only/AES/required-PMF, target=");print_ssid(ap);console_write(" BSSID=");print_mac(ap->bssid);
    console_write("\nMay send join discovery traffic; wait up to 15 seconds plus I/O for external-SAE request, then DOWN/restore. No password or SAE response.\n");
    void (*saved_event)(void *,const void *,size_t)=control.event;void *saved_context=control.event_context;
    control.event=brcm_join_event;control.event_context=&join_probe;control.rx_limit=control.rx_frames+1024;
    const struct brcm_join_probe_io io={{NULL,prepare_iovar},join_station,scan_radio,scan_poll,firmware_delay,scan_milliseconds,join_poison,join_station_live,NULL,0};
    (void)brcm_join_probe_run(&io,&peer,true,&join_probe);
    control.event=saved_event;control.event_context=saved_context;control.rx_limit=control.rx_frames+64;
    if (join_probe.policy.changed && !join_probe.policy.restored) { join_poison(NULL,join_probe.policy.restore_error); }
    join_probe_status();transfer_failure();
}
static bool sae_random(void *ctx,unsigned char out[32])
{ (void)ctx;return platform_random_bytes(out,32)==BLOCK_OK; }
static bool prepare_sae_commit(const struct brcm_auth_peer *peer,unsigned char out[98],struct sae_session *retained)
{
    struct { unsigned char password[128],pt[64],pwe[64],check[32];struct sae_session session; } w={0};
    bool ok=false;size_t password_bytes=0;uint64_t start=0;
    crypto_wipe(out,98);platform_rng_test(firmware_dtb);
    if (platform_random_bytes(w.check,sizeof(w.check))!=BLOCK_OK) { console_write("Hardware RNG unavailable; SAE probe refused before credential input.\n");goto done; }
    crypto_wipe(w.check,sizeof(w.check));
    console_write("Password (hidden, printable ASCII; Enter submits, Ctrl+U clears, empty Enter cancels): ");
    if (!console_read_secret(w.password,sizeof(w.password),&password_bytes)) { console_write("\nInput cancelled or invalid; no radio activity.\n");goto done; }
    console_write(peer->hunting ? "\nPreparing group-19 SAE hunting-and-pecking PWE: fixed 40 candidates before radio UP. Wait for the prompt.\n" : "\nPreparing group-19 H2E token before radio UP. Wait for the prompt.\n");
    sae_preparation_attempted=true;start=timer_ticks();
    if (peer->hunting) {
        ok=sae_hunt_pwe(w.password,password_bytes,peer->local,peer->peer,w.pwe);
        crypto_wipe(w.password,sizeof(w.password));password_bytes=0;if (!ok) { goto done; }
        console_write("Hunting-and-pecking password element ready; password erased.\n");
    }else {
        ok=sae_h2e_pt(peer->ssid,peer->ssid_length,w.password,password_bytes,NULL,0,w.pt);
        crypto_wipe(w.password,sizeof(w.password));password_bytes=0;if (!ok) { goto done; }
        console_write("H2E token ready; password erased. Deriving the MAC-bound password element.\n");
        ok=sae_h2e_pwe(w.pt,peer->local,peer->peer,w.pwe);crypto_wipe(w.pt,sizeof(w.pt));if (!ok) { goto done; }
    }
    console_write("Password element ready. Preparing one fresh randomized SAE commit; no radio UP yet.\n");
    ok=sae_session_start(&w.session,w.pwe,sae_random,NULL)==SAE_OK && sae_session_commit(&w.session,out)==SAE_OK;
    if (ok && retained) { *retained=w.session; }
done:
    crypto_wipe(&w,sizeof(w));if (!ok) { crypto_wipe(out,98); }
    if (sae_preparation_attempted) {
        console_write("SAE preparation elapsed_seconds=");uint64_t hz=timer_frequency();console_u64(hz ? (timer_ticks()-start)/hz : 0);
        console_write(ok ? (retained ? " password/token temporaries erased; private session retained only for this confirm test.\n" : " private state erased; commit ready.\n") : " failed; buffers erased; reboot before retry.\n");
    }
    return ok;
}
static void sae_status(const struct brcm_sae_probe *s)
{
    console_write("SAE method=");console_write(s->join.peer.hunting ? "hunting-and-pecking (commit status 0)" : "H2E (commit status 126)");console_write("\n");
    console_write(s->confirm_mode ? "SAE confirm test: " : "SAE commit probe: ");console_write(brcm_auth_error(s->error));console_write(" phase=");console_u64(s->phase);
    console_write(" join_phase=");console_u64(s->join.phase);console_write("\n");
    console_write("request_seen=");console_write(s->join.request_seen ? "yes" : "no");
    console_write(" commit_TX_attempted=");console_write(s->tx_attempted ? "yes" : "no");
    console_write(" SET_ack=");console_write(s->tx_set_acknowledged ? "yes" : "no");console_write(" TX_status=");
    if (s->tx_status_seen) { console_u64(s->tx_status); }else { console_write("not-seen"); }
    console_write(" offchannel=");if (s->offchannel_seen) { console_u64(s->offchannel_status); }else { console_write("not-seen"); }console_write("\n");
    console_write("peer_RX=");console_write(s->rx_seen ? "seen" : "not-seen");console_write(" metadata_version=");console_u64(s->metadata_version);
    console_write(" sequence=");console_u64(s->sequence);console_write(" status=");console_u64(s->auth_status);console_write(" group=");console_u64(s->group);
    console_write(" body_bytes=");console_u64(s->wire_body_bytes);console_write(" peer_commit_valid=");console_write(s->peer_valid ? "yes" : "no");
    console_write(" reflection=");console_write(s->reflection ? "yes" : "no");console_write("\n");
    console_write("events=");console_u64(s->events);console_write(" ignored=");console_u64(s->ignored);console_write(" polls=");console_u64(s->polls);
    console_write(" firmware_status=");console_hex(s->firmware_status);console_write("\n");
    console_write("commit_attempts=");console_u64(s->commit_attempts);console_write(" active_packet_id=");console_u64(s->active_packet_id);
    console_write(" observed_auth_RX=");console_u64(s->wire_auth_rx);console_write(" early_RX=");console_u64(s->rx_early);
    console_write(" filtered_RX=");console_u64(s->rx_filtered);console_write(" malformed_RX=");console_u64(s->rx_bad);console_write("\n");
    if (s->confirm_mode) {
        console_write("confirm_TX_attempted=");console_write(s->confirm_tx_attempted ? "yes" : "no");
        console_write(" SET_ack=");console_write(s->confirm_set_acknowledged ? "yes" : "no");console_write(" TX_status=");
        if (s->confirm_tx_status_seen) { console_u64(s->confirm_tx_status); }else { console_write("not-seen"); }
        console_write(" confirm_RX=");console_write(s->confirm_rx_seen ? "seen" : "not-seen");
        console_write(" peer_confirm_verified=");console_write(s->confirmed ? "yes" : "no");console_write(" crypto_result=");
        if (s->crypto_attempted) { console_u64(s->crypto_result); }else { console_write("not-run"); }
        console_write(" key_math_ms=");console_u64(s->key_math_ms);console_write(" firmware_status=");console_hex(s->confirm_firmware_status);console_write("\n");
    }
    console_write("Interface DOWN=");console_write(s->join.down ? "acknowledged" : s->join.up_attempted ? "UNCONFIRMED" : "not-raised");
    console_write(" policy/mask restoration=");console_write(s->join.policy.restored ? "verified" : s->join.policy.changed ? "UNCONFIRMED" : "not-needed");
    console_write(" transcript_buffers=");console_write(s->erased ? "erased" : "not-used");console_write("\n");
    unsigned count=s->trace_count>BRCM_SAE_TRACE_MAX ? BRCM_SAE_TRACE_MAX : s->trace_count;
    console_write("Event diagnostics (last ");console_u64(count);console_write("; stage 0=before TX, 1=commit, 2=confirm, 3=handoff):\n");
    unsigned start=s->trace_count-count;
    for (unsigned i=0;i<count;++i) {
        const struct brcm_sae_event_trace *t=&s->trace[(start+i)%BRCM_SAE_TRACE_MAX];
        console_write("event=");console_u64(t->event);console_write(" status=");console_hex(t->status);console_write(" bytes=");console_u64(t->bytes);
        console_write(" stage=");console_u64(t->stage);console_write(" cookie=");console_u64(t->cookie);console_write(" if=");console_u64(t->interface);
        console_write(" bss=");console_u64(t->bss);console_write(" filter=");console_u64(t->filter);console_write(" result=");console_write(brcm_auth_error(t->result));console_write("\n");
    }
    if (s->handoff_attempted) { console_write("SAE verified; firmware handoff was attempted. See association status. Private session/keys erased.\n"); }
    else if (s->confirm_mode) { console_write("SAE confirm test only; private session/keys erased. No firmware auth-success, association, EAPOL/key installation or IP. Reboot before retry.\n"); }
    else { console_write("Commit/reply observation only. Password correctness, SAE confirms, key installation and IP are not established. Reboot before retry.\n"); }
}
static void assoc_status(void)
{
    const struct brcm_assoc_probe *s=&association_probe;
    console_write("Association/EAPOL probe: ");console_write(s->port_attempted ? block_error(s->error) : brcm_auth_error(s->error));console_write(" phase=");console_u64(s->phase);console_write("\n");
    if (s->dhcp.attempted && s->dhcp.error!=BLOCK_OK) { console_write("Failure stage: DHCP/data path after verified SAE; see netinfo and SCB snapshots.\n"); }
    else if (s->gateway_attempted && s->gateway_result!=BLOCK_OK) { console_write("Failure stage: address checks/ARP/gateway ICMP; see netinfo.\n"); }
    sae_status(&s->sae);
    console_write("RSN mode=");console_write(s->ie_owned ? "exclusive fresh-station SET/clear journal" : "snapshot/readback");console_write("\n");
    if (s->ie_owned) {
        console_write("RSN empty_baseline_SET=");console_write(s->ie_baseline_ack ? "acknowledged" : "not-acknowledged");
        console_write(" profile_SET=");console_write(s->ie_set_ack ? "acknowledged" : "not-acknowledged");
        console_write(" cleanup_clear_SET=");console_write(s->ie_clear_ack ? "acknowledged" : s->ie_changed ? "UNCONFIRMED" : "not-needed");console_write("\n");
        console_write("wpaie GET diagnostic only: result=");console_write(brcm_auth_error(s->ie_query_result));console_write(" firmware_status=");console_hex(s->ie_query_status);
        console_write(" bytes=");console_u64(s->ie_query_bytes);console_write(" prefix_LE32:");
        for (unsigned i=0;i<sizeof(s->ie_query_prefix) && i<s->ie_query_bytes;i+=4) {
            uint32_t word=0;for (unsigned j=0;j<4 && i+j<s->ie_query_bytes;++j) { word|=(uint32_t)s->ie_query_prefix[i+j]<<(8*j); }console_write(" ");console_hex(word);
        }console_write("\n");
    }
    console_write("RSN snapshot=");console_write(s->ie_snapshot ? "saved" : s->ie_owned ? "software-owned empty baseline" : "not-saved");console_write(" bytes=");console_u64(s->saved_ie_bytes);
    console_write(" effective_request=");console_write(s->ie_verified ? "verified" : "unverified");console_write(" restored=");console_write(s->ie_restored ? s->ie_owned ? "clear acknowledged" : "verified" : s->ie_changed ? "UNCONFIRMED" : "not-needed");
    console_write(" result=");console_write(brcm_auth_error(s->ie_error));console_write(" restore_result=");console_write(brcm_auth_error(s->restore_error));console_write("\n");
    console_write("firmware_auth_success_SET=");console_write(s->status_ack ? "acknowledged" : s->status_issued ? "FAILED" : "not-issued");console_write(" status=");console_hex(s->auth_status);
    console_write(" private_keys_erased=");console_write(s->keys_erased ? "yes" : "not-exported");console_write("\n");
    console_write("SET_SSID_success=");console_write(s->ssid_event ? "seen" : "not-seen");console_write(" LINK_up=");console_write(s->link_event ? "seen" : "not-seen");
    console_write(" identity=");console_write(s->identity_verified ? "verified" : "unverified");console_write(" SAE/AES/PMF=");console_write(s->policy_verified ? "verified" : "unverified");
    console_write(" association_verified=");console_write(s->association_verified ? "yes" : "no");console_write("\n");
    console_write("Association request IEs=");console_write(s->assoc_ie_verified ? "verified" : "unverified");console_write(" bytes=");console_u64(s->assoc_ie_bytes);
    console_write(" RSNXE_H2E=");console_write(s->request_h2e ? "present" : "not-seen");console_write(" firmware_status=");console_hex(s->identity_status);console_write("\n");
    console_write("Association IE query phase=");console_u64(s->assoc_query_phase);console_write(" info_reply_bytes=");console_u64(s->assoc_info_reply_bytes);
    console_write(" request_reply_bytes=");console_u64(s->assoc_req_reply_bytes);console_write(" info_header_LE32:");
    for (unsigned i=0;i<3;++i) { console_write(" ");console_hex(s->assoc_info_words[i]); }console_write("\n");
    console_write("Association request fixed_header_bytes=");console_u64(s->request_fixed_bytes);console_write(" normalized_IE_bytes=");console_u64(s->assoc_ie_bytes);console_write("\n");
    static const char *const ie_errors[]={"ok","truncated TLV","SSID mismatch/duplicate","duplicate RSN","RSN security fields","RSNXE/method","WPA1 IE","missing SSID","missing RSN"};
    console_write("Request IE check=");console_write(s->request_error<9 ? ie_errors[s->request_error] : "unknown");console_write(" offset=");console_u64(s->request_error_offset);
    console_write(" tag=");console_u64(s->request_tag);console_write(" RSN_bytes=");console_u64(s->request_rsn_bytes);console_write(" RSN_result=");console_write(wpa_rsn_error(s->request_rsn_error));
    console_write(" capabilities=");console_hex(s->request_rsn.capabilities);console_write(" PMKID_count=");console_u64(s->request_rsn.pmkid_count);
    console_write(" management_suite=");console_write(s->request_rsn.management_explicit ? "explicit" : "BIP-CMAC-128 default if otherwise valid");console_write("\n");
    if (s->request_error && s->request_saved) {
        static const char hex[]="0123456789abcdef";console_write("Firmware association-request IEs (public wire data; no password or keys):\n");
        for (size_t i=0;i<s->request_saved;++i) {
            if (!(i%16)) { console_u64(i);console_write(": "); }
            console_putc(hex[s->request_ies[i]>>4]);console_putc(hex[s->request_ies[i]&15]);console_putc(' ');
            if (i%16==15 || i+1==s->request_saved) { console_write("\n"); }
        }
    }
    console_write("EAPOL_M1=");console_write(s->m1_seen ? "observed (unauthenticated)" : "not-seen");console_write(" version=");console_u64(s->eapol_version);
    console_write(" descriptor=");console_u64(s->descriptor);console_write(" last_key_info=");console_hex(s->key_info);
    console_write(" data_frames=");console_u64(s->data_frames);console_write(" ignored=");console_u64(s->data_ignored);console_write(" malformed=");console_u64(s->data_bad);console_write("\n");
    if (s->key_probe) {
        console_write("EAPOL key probe: ");console_write(wpa_probe_error(s->key_result));console_write(" phase=");console_u64(s->key_phase);
        console_write(" M2_SDIO_write=");console_write(s->m2_written ? "completed (not air ACK)" : s->m2_attempted ? "FAILED" : "not-attempted");console_write(" bytes=");console_u64(s->m2_bytes);console_write("\n");
        console_write("M3_bytes=");console_u64(s->m3_bytes);console_write(" MIC=");console_write(s->m3_mic_verified ? "verified" : "unverified");
        console_write(" unwrap_integrity=");console_write(s->m3_unwrap_verified ? "verified" : "unverified");console_write(" plaintext_bytes=");console_u64(s->unwrapped_bytes);console_write(" (private buffers erased on exit)\n");
        if (s->key_data_probe) {
            console_write("M3 key data: ");console_write(wpa_kde_error(s->key_data_result));console_write(" verified=");console_write(s->key_data_verified ? "yes" : "no");
            console_write(" AP_RSN_scan_match=");console_write(s->key_data.rsn_match ? "yes" : "no");console_write(" RSN_bytes=");console_u64(s->key_data.rsn_bytes);console_write("\n");
            console_write("GTK_bytes=");console_u64(s->key_data.gtk_bytes);console_write(" id=");console_u64(s->key_data.gtk_id);
            console_write(" IGTK_bytes=");console_u64(s->key_data.igtk_bytes);console_write(" id=");console_u64(s->key_data.igtk_id);console_write(" padding_bytes=");console_u64(s->key_data.padding_bytes);console_write(" (key bytes erased)\n");
            console_write("KDE check offset=");console_u64(s->key_data.error_offset);console_write(" tag=");console_u64(s->key_data.tag);console_write(" selector=");console_u64(s->key_data.selector);console_write("\n");
            console_write("M4_encoded=");console_write(s->m4_encoded ? "yes" : "no");console_write(" bytes=");console_u64(s->m4_bytes);
            console_write(" M4_SDIO_write=");console_write(s->m4_written ? "completed (not air ACK)" : s->m4_attempted ? "FAILED" : "not-attempted");console_write("\n");
        }
    }
    if (s->install_probe) {
        console_write("M3 retries: queued=");console_u64(s->m3_retries_queued);console_write(" authenticated=");console_u64(s->m3_retries_verified);
        console_write(" duplicates=");console_u64(s->m3_duplicates);console_write(" pending=");console_write(s->m3_retry_pending ? "yes" : "no");console_write("\n");
        static const char *const retry_filters[]={"none","counter not above M1","counter below latest M3","changed bytes at same M3 counter","conflicting pending candidate"};
        console_write("M3 retry replay filter=");console_write(s->m3_retry_rejection<5 ? retry_filters[s->m3_retry_rejection] : "unknown");console_write("\n");
        console_write("M4 writes=");console_u64(s->m4_writes);console_write(" credit_deferrals=");console_u64(s->m4_deferred);console_write(" key_reinstall_attempts=0 (one-shot install journal)\n");
        console_write("Firmware key installation: ");console_write(s->keys.attempted ? brcm_auth_error(s->keys.error) : "not-attempted");console_write(" phase=");console_u64(s->keys.phase);
        console_write(" issued_mask=");console_hex(s->keys.issued_mask);console_write(" ack_mask=");console_hex(s->keys.ack_mask);console_write(" status=");console_hex(s->keys.status);console_write(" (SET acknowledgements; no key readback)\n");
        console_write("Post-M4 identity=");console_write(s->post_m4_identity ? "verified" : "unverified");console_write(" settle_polls=");console_u64(s->settle_polls);console_write("\n");
        console_write("Firmware key cleanup=");console_write(!s->keys.issued_mask ? "not-needed" : s->keys.cleared ? "acknowledged" : "UNCONFIRMED");
        console_write(" clear_mask=");console_hex(s->keys.clear_mask);console_write(" result=");console_write(brcm_auth_error(s->keys.clear_error));console_write(" status=");console_hex(s->keys.clear_status);console_write("\n");
    }
    console_write("association events=");console_u64(s->events);console_write(" ignored=");console_u64(s->ignored);console_write(" polls=");console_u64(s->polls);
    console_write(" identity_queries=");console_u64(s->identity_queries);console_write(" last_event=");console_u64(s->last_event);
    console_write(" status=");console_hex(s->last_status);console_write(" reason=");console_u64(s->last_reason);console_write(" flags=");console_hex(s->last_flags);console_write("\n");
    if (s->event_error!=BLOCK_OK) { console_write("failure_event=");console_u64(s->failure_event);console_write(" status=");console_hex(s->failure_status);console_write(" reason=");console_u64(s->failure_reason);console_write("\n"); }
    console_write(s->sae.join.down ? "Probe ended DOWN. " : s->sae.join.up_attempted ? "Interface DOWN is UNCONFIRMED. " : "Radio UP was not attempted. ");
    console_write("No persistent authorized data port or IP. Bounded test only; reboot before retry.\n");
}
static enum block_result assoc_identity(void *ctx,struct brcm_link_identity *out,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_identity(&io,&control,out,status);
}
static bool assoc_m2_permitted(void *ctx)
{
    const struct brcm_assoc_probe *s=ctx;
    return s && s->armed && s->status_ack && s->association_verified && s->assoc_ie_verified && s->m1_seen && s->sae.confirmed &&
        s->sae.event_error==BLOCK_OK && s->event_error==BLOCK_OK && s->private_exchange && !s->private_exchange->failed && s->private_exchange->m2_issued && !s->m2_written;
}
static enum block_result assoc_data_tx(void *ctx,const void *frame,size_t n)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_eapol_m2(&io,&control,frame,n,assoc_m2_permitted,&association_probe);
}
static bool assoc_key_permitted(void *ctx)
{
    const bool *remove=ctx;const struct brcm_assoc_probe *s=&association_probe;
    if (*remove) { return s->keys.cleanup_active; }
    return s->armed && s->status_ack && s->association_verified && s->key_data_verified && s->event_error==BLOCK_OK && s->sae.event_error==BLOCK_OK && s->private_exchange && s->private_exchange->group_retained;
}
static enum block_result assoc_key_set(void *ctx,const void *wire,size_t n,uint32_t *status)
{
    (void)ctx;if (!brcm_key_wire_valid(wire,n)) { return BLOCK_ARGUMENT; }bool remove=!brcm_key_le32((const unsigned char *)wire+4);
    struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_key(&io,&control,wire,n,status,assoc_key_permitted,&remove);
}
static bool assoc_m4_permitted(void *ctx)
{
    (void)ctx;bool remove=false;const struct brcm_assoc_probe *s=&association_probe;
    return assoc_key_permitted(&remove) && ((!s->keys.attempted && !s->keys.issued_mask) || (s->keys.installed && s->keys.ack_mask==7)) && s->m4_encoded && s->m4_attempted && s->m4_writes<4 &&
        s->private_exchange->m4_encoded && !s->private_exchange->retry_pending && !s->private_exchange->m4_due && !s->private_exchange->failed;
}
static enum block_result assoc_m4_tx(void *ctx,const void *frame,size_t n)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_eapol_m4(&io,&control,frame,n,assoc_m4_permitted,NULL);
}
static bool assoc_dhcp_permitted(void *ctx)
{
    (void)ctx;bool remove=false;const struct brcm_assoc_probe *s=&association_probe;
    return assoc_key_permitted(&remove) && s->port_verified && s->dhcp_probe && (s->dhcp.armed || (s->gateway_probe && s->decline_active && s->network.conflict)) && s->post_m4_identity && s->m4_written && s->keys.installed &&
        !s->private_exchange->retry_pending && !s->private_exchange->m4_due && !s->private_exchange->failed;
}
static enum block_result assoc_dhcp_tx(void *ctx,const void *frame,size_t n)
{
    const struct brcm_assoc_probe *s=&association_probe;const unsigned char *p=frame;
    if (n!=DHCP_FRAME_BYTES || !p || (p[284]==4 ? !s->decline_active : !s->dhcp.armed)) { return BLOCK_NOT_READY; }
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_dhcp(&io,&control,frame,n,assoc_dhcp_permitted,NULL);
}
static bool assoc_net_permitted(void *ctx)
{
    (void)ctx;const struct brcm_assoc_probe *s=&association_probe;bool remove=false;
    return assoc_key_permitted(&remove) && s->port_verified && s->gateway_probe && s->keys.installed && s->m4_written && s->post_m4_identity &&
        !s->private_exchange->retry_pending && !s->private_exchange->m4_due && !s->private_exchange->failed && netprobe_can_tx(&s->network,scan_milliseconds(NULL));
}
static enum block_result assoc_net_tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *frame,size_t n)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_netprobe(&io,&control,&association_probe.network,kind,sequence,frame,n,assoc_net_permitted,NULL);
}
static bool assoc_port_permitted(void *ctx)
{
    (void)ctx;const struct brcm_assoc_probe *s=&association_probe;if (s->port_close_active) { return s->port_issued; }
    bool remove=false;return assoc_key_permitted(&remove) && s->dhcp_probe && s->port_attempted && s->post_m4_identity && s->m4_written && s->keys.installed &&
        !s->private_exchange->retry_pending && !s->private_exchange->m4_due && !s->private_exchange->failed;
}
static enum block_result assoc_port_set(void *ctx,const unsigned char peer[6],bool authorize,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_port(&io,&control,peer,authorize,status,assoc_port_permitted,NULL);
}
static enum block_result assoc_peer_info(void *ctx,const unsigned char peer[6],struct brcm_station_info *out,uint32_t *status)
{
    (void)ctx;struct brcm_control_io io={NULL,control_read8,control_write8,core_read,control_write32,control_packet,firmware_delay};
    return brcm_control_station_info(&io,&control,peer,out,status);
}
static void peer_status(const char *label,const struct brcm_station_info *s)
{
    console_write(label);console_write(" valid=");console_write(s->valid ? "yes" : "no");console_write(" version=");console_u64(s->version);console_write(" bytes=");console_u64(s->length);console_write(" flags=");console_hex(s->flags);
    console_write(" authenticated=");console_write(s->flags&8 ? "yes" : "no");console_write(" associated=");console_write(s->flags&16 ? "yes" : "no");console_write(" authorized=");console_write(s->flags&32 ? "yes" : "no");console_write("\n");
    if (s->statistics) {
        console_write("SCB counters tx=");console_u64(s->tx_packets);console_write(" tx_fail=");console_u64(s->tx_failures);console_write(" rx_unicast=");console_u64(s->rx_unicast);console_write(" rx_multicast=");console_u64(s->rx_multicast);
        console_write(" decrypt_ok=");console_u64(s->decrypt_ok);console_write(" decrypt_fail=");console_u64(s->decrypt_fail);console_write("\n");
    }
}
static void ipv4(const unsigned char p[4])
{ for (unsigned i=0;i<4;++i) { if (i) { console_putc('.'); }console_u64(p[i]); } }
void platform_network_info(void)
{
    const struct brcm_assoc_probe *s=&association_probe;
    console_write("First M4 before TX key installation=");console_write(s->first_m4_before_keys ? "yes" : "no");console_write(" (SDIO ordering only, not air ACK)\n");
    console_write("Controlled port: ");console_write(s->port_attempted ? block_error(s->port_result) : "not-attempted");console_write(" phase=");console_u64(s->port_phase);console_write(" AUTHORIZE_SET=");console_write(s->port_ack ? "acknowledged" : s->port_issued ? "unconfirmed" : "not-issued");
    console_write(" readback=");console_write(s->port_verified ? "verified" : "not-verified");console_write(" firmware_status=");console_hex(s->port_status);console_write(" deferrals=");console_u64(s->port_deferrals);console_write("\n");
    if (s->port_attempted) { peer_status("SCB before",&s->port_before);peer_status("SCB after",&s->port_after); }
    if (s->dhcp.attempted) { console_write("SCB final query: ");console_write(block_error(s->port_final_result));console_write(" firmware_status=");console_hex(s->port_final_status);console_write("\n");peer_status("SCB final",&s->port_final); }
    console_write("DEAUTHORIZE_SET=");console_write(s->port_closed ? "acknowledged" : s->port_issued ? "UNCONFIRMED" : "not-needed");console_write(" status=");console_hex(s->port_close_status);console_write("\n");
    const struct dhcp_client *d=&association_probe.dhcp;
    console_write("DHCP probe: ");console_write(d->attempted ? d->error==BLOCK_TIMEOUT ? "reply deadline expired" : block_error(d->error) : "not-attempted");console_write(" phase=");console_u64(d->phase);console_write("\n");
    console_write("DISCOVER_writes=");console_u64(d->discover_sent);console_write(" REQUEST_writes=");console_u64(d->request_sent);console_write(" OFFER=");console_write(d->have_offer ? "observed" : "not-seen");
    console_write(" ACK=");console_write(d->ack_seen && d->request_written ? "verified for transaction/server" : "not-verified");console_write(" NAK=");console_write(d->nak_seen ? "seen" : "not-seen");console_write("\n");
    console_write("DHCP RX=");console_u64(d->received);console_write(" ignored=");console_u64(d->ignored);console_write(" malformed=");console_u64(d->malformed);console_write(" credit_deferrals=");console_u64(d->deferrals);console_write(" polls=");console_u64(d->polls);console_write("\n");
    const struct dhcp_lease *l=d->ack_seen ? &d->lease : &d->offer;
    if (d->have_offer) {
        console_write(d->ack_seen ? "ACK address=" : "Offered address=");ipv4(l->address);console_write(" server=");ipv4(l->server);console_write(" lease_seconds=");console_u64(l->seconds);console_write("\n");
        console_write("mask=");if (l->have_mask) { ipv4(l->mask); }else { console_write("not-supplied"); }
        console_write(" gateway=");if (l->have_router) { ipv4(l->router); }else { console_write("not-supplied"); }
        console_write(" DNS=");if (l->have_dns) { ipv4(l->dns); }else { console_write("not-supplied"); }console_write("\n");
    }
    if (s->gateway_probe) {
        const struct netprobe *n=&s->network;
        console_write("Gateway probe: ");console_write(s->gateway_attempted ? block_error(s->gateway_result) : "not-attempted");console_write(" phase=");console_u64(n->phase);console_write("\n");
        console_write("ARP checks: probes=");console_u64(n->probes);console_write(" announcements=");console_u64(n->announcements);console_write(" conflict=");console_write(n->conflict ? "yes" : "not-observed");
        console_write(" temporary_IP_used=");console_write(n->was_active ? "yes" : "no");console_write(" active_now=");console_write(n->active ? "yes" : "no");console_write("\n");
        console_write("Gateway ARP requests=");console_u64(n->resolutions);console_write(" learned=");console_write(n->gateway_known ? "yes" : "no");console_write(" MAC=");print_mac(n->gateway_mac);console_write(" changed=");console_write(n->gateway_changed ? "yes" : "no");console_write("\n");
        console_write("ICMP to gateway ");ipv4(n->lease.router);console_write(" sent=");console_u64(n->echoes);console_write(" reply_mask=");console_hex(n->received_mask);console_write("\n");
        for (unsigned j=0;j<3;++j) { if (n->received_mask&(1u<<j)) { console_write("echo sequence=");console_u64(j+1);console_write(" observed_latency_ms=");console_u64(n->latency_ms[j]);console_write(" (includes polling/I/O)\n"); } }
        console_write("Network RX=");console_u64(n->received);console_write(" ARP=");console_u64(n->arp_rx);console_write(" IPv4=");console_u64(n->ipv4_rx);console_write(" ignored=");console_u64(n->ignored);console_write(" malformed=");console_u64(n->malformed);console_write(" deferrals=");console_u64(n->deferrals);console_write(" polls=");console_u64(n->polls);console_write(" ARP_replies=");console_u64(n->arp_replies);console_write("\n");
        if (n->conflict) { console_write("Conflicting MAC=");print_mac(n->conflict_mac);console_write(" DHCPDECLINE_write=");console_write(s->decline_written ? "completed" : "unconfirmed");console_write(" result=");console_write(block_error(s->decline_result));console_write("\n"); }
        console_write("Cached gateway test only; temporary IP removed and Wi-Fi ends DOWN. No DNS, external Internet probe or persistent connection.\n");
    }else { console_write("Cached DHCP metadata only; no address installed. Use wifiping on a fresh boot for ARP/gateway testing. Wi-Fi ends DOWN.\n"); }
}
static void run_sae(const char *ssid,unsigned mode)
{
    bool authenticate=mode!=0,association=mode>=2;
    if (sae_preparation_attempted || sae_probe.attempted || association_probe.attempted) { console_write("SAE preparation/probe already attempted; cached status. Reboot to repeat.\n");if (association_probe.attempted) { assoc_status(); }else { sae_status(&sae_probe); }return; }
    if (join_probe.attempted) { console_write("Join probe already used this boot. Reboot and use the selected SAE command directly after scan/target.\n");return; }
    size_t n=0;while (ssid && ssid[n] && n<=32) { ++n; }const struct brcm_scan_ap *ap=NULL;
    enum wifi_target_result target=wifi_target_check(&scan,&capabilities,ssid,n,&ap);
    if (target!=WIFI_TARGET_OK) { console_write("SAE probe refused: ");console_write(wifi_target_error(target));console_write("\n");return; }
    enum wifi_sae_method method=wifi_target_sae_method(ap);
    if (method==WIFI_SAE_METHOD_INVALID) { console_write("SAE probe refused: malformed method advertisement.\n");return; }
    if (association && !wpa_ap_rsn_profile(ap->rsn,ap->rsn_bytes)) { console_write("Key-data probe refused: unsupported/missing AP RSN snapshot. Requires CCMP, SAE optionally with PSK, and PMF; no credentials requested.\n");return; }
    if (!capabilities.external_sae || !card.selected || !control.ready || control.error!=BLOCK_OK || control.ioctl_version!=2 || !preparation.ready || ap->band!=2 || !ap->channel || ap->channel>13) {
        console_write("SAE probe requires clean 2.4 GHz scan/target, external-SAE capability and live prepared control.\n");return;
    }
    struct brcm_auth_peer peer={.ssid_length=ap->ssid_length,.channel=ap->channel,.band=ap->band,.hunting=method==WIFI_SAE_HUNT};
    for (unsigned i=0;i<6;++i) { peer.local[i]=capabilities.mac[i];peer.peer[i]=ap->bssid[i]; }for (unsigned i=0;i<ap->ssid_length;++i) { peer.ssid[i]=ap->ssid[i]; }
    console_write("SAE method selected from scan: ");console_write(peer.hunting ? "hunting-and-pecking (H2E not advertised)" : "H2E");console_write("; SAE-only, required PMF; no method retry or WPA2 fallback.\n");
    console_write(association ? "One-shot association/EAPOL observation for " : authenticate ? "One-shot SAE commit/confirm test for " : "One-shot SAE commit probe for ");print_ssid(ap);console_write(" BSSID=");print_mac(ap->bssid);
    console_write(association ? ". Verified SAE/M3 required, then initial M4 before TX keys, PTK/GTK/IGTK installation and identity checks; then DOWN/delete/restore. No persistent connection.\n" : authenticate ? ". Will verify a peer confirm, then DOWN/restore; no association or IP.\n" : ". No confirm, auth success, keys or IP will be sent.\n");
    if (mode>=3) { console_write("After verified M4/keys/identity: authorize peer port, verify sta_info, then DHCP test up to 45 seconds plus I/O.\n"); }
    if (mode==4) { console_write("After DHCP: three address probes, announcements, gateway ARP and three ICMP echoes; up to 30 seconds plus I/O. Temporary IP removed on exit.\n"); }
    unsigned char commit[98]={0};struct sae_session session={0};
    if (!prepare_sae_commit(&peer,commit,authenticate ? &session : NULL)) { crypto_wipe(commit,sizeof(commit));sae_session_clear(&session);return; }
    console_write("Starting bounded join/SAE exchange, then DOWN/restore.\n");
    void (*saved_event)(void *,const void *,size_t)=control.event;void *saved_context=control.event_context;
    void (*saved_data)(void *,const void *,size_t)=control.data_observer;void *saved_data_context=control.data_context;
    control.event=brcm_sae_probe_event;control.event_context=&sae_probe;control.rx_limit=control.rx_frames+1024;
    const struct brcm_join_probe_io io={{NULL,prepare_iovar},join_station,scan_radio,scan_poll,firmware_delay,scan_milliseconds,join_poison,join_station_live,NULL,0};
    if (association) {
        control.event=brcm_assoc_event;control.event_context=&association_probe;control.data_observer=brcm_assoc_data;control.data_context=&association_probe;
        /* Only this guarded one-shot path sets wpaie. Fresh control, clean
         * scan/DOWN and no earlier join/SAE attempt establish IE ownership. */
        const struct brcm_assoc_io aio={.join=io,.identity=assoc_identity,.fresh_ie_owner=true,.request_fixed_bytes=4,.key_probe=true,.data_tx=assoc_data_tx,.random=sae_random,.key_data_probe=true,.ap_rsn=ap->rsn,.ap_rsn_bytes=ap->rsn_bytes,.install_probe=true,.key_set=assoc_key_set,.m4_tx=assoc_m4_tx,.dhcp_probe=mode>=3,.dhcp_tx=assoc_dhcp_tx,.peer_info=assoc_peer_info,.port_set=assoc_port_set,.gateway_probe=mode==4,.net_tx=assoc_net_tx};
        (void)brcm_assoc_run(&aio,&peer,&session,true,&association_probe);
    }
    else if (authenticate) { (void)brcm_sae_auth_run(&io,&peer,&session,1,true,&sae_probe); }
    else { (void)brcm_sae_probe_run(&io,&peer,commit,1,true,&sae_probe); }
    crypto_wipe(commit,sizeof(commit));sae_session_clear(&session);
    control.event=saved_event;control.event_context=saved_context;control.rx_limit=control.rx_frames+64;
    control.data_observer=saved_data;control.data_context=saved_data_context;
    if (association) { assoc_status();if (mode>=3) { platform_network_info(); } }else { sae_status(&sae_probe); }transfer_failure();
}
void platform_wifi_sae_probe(const char *ssid) { run_sae(ssid,0); }
void platform_wifi_sae_auth(const char *ssid) { run_sae(ssid,1); }
void platform_wifi_assoc(const char *ssid) { run_sae(ssid,2); }
void platform_wifi_dhcp(const char *ssid) { run_sae(ssid,3); }
void platform_wifi_ping(const char *ssid) { run_sae(ssid,4); }
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
    if (join_probe.attempted) { join_probe_status(); }
    if (sae_probe.attempted) { sae_status(&sae_probe); }
    if (association_probe.attempted) { assoc_status(); }
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
