#include "sdhci.h"
#include "console.h"
#include "cpu.h"
#include "resources.h"
#include "xhci_test.h"
/* Pi DT mmc0 /soc@107c000000/mmc@fff000 translated via soc ranges. */
#define SD_BASE 0x1000fff000UL
#define SD_GPIO_DATA 0x107d517c04ULL
#define SD_PIN_SELECT 0x444
#define SD_GPIO_MASK (7u<<3)
#define SD_GPIO_READY (1u<<4)
static struct sdhci_host host={.error=BLOCK_NOT_READY};
static bool usb_backend;
static uint32_t read_reg(void *context,unsigned offset)
{
    (void)context; uint32_t value=*(volatile uint32_t *)(SD_BASE+offset);
    __asm__ volatile("dmb sy" ::: "memory"); return value;
}
static void write_reg(void *context,unsigned offset,uint32_t value)
{
    (void)context; __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)(SD_BASE+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void write_byte(void *context,unsigned offset,uint8_t value)
{
    (void)context;
    *(volatile uint8_t *)(SD_BASE+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }
static bool probe_read(void *context,uint64_t address,uint32_t *value)
{
    (void)context;
    *value=*(volatile uint32_t *)(uintptr_t)address;
    __asm__ volatile("dmb sy" ::: "memory");
    return *value!=UINT32_MAX;
}
void pi5_sd_probe(const void *dtb)
{
    struct pi5_sd_snapshot s;
    enum block_result result=pi5_sd_snapshot(dtb,0x200000,probe_read,NULL,&s);
    if (result!=BLOCK_OK) {
        console_write("microSD probe: "); console_write(block_error(result)); console_write("\n"); return;
    }
    console_write("microSD present="); console_write(s.gpio_levels&(1u<<5) ? "no" : "yes");
    console_write(" host_present="); console_write(s.present&(1u<<16) ? "yes" : "no");
    console_write(" stable="); console_write(s.present&(1u<<17) ? "yes" : "no");
    console_write(" cd_gpio_level="); console_u64((s.gpio_levels>>5)&1);
    console_write(" host_power="); console_write(s.control&(1u<<8) ? "on" : "off");
    console_write(" card_clock="); console_write((s.clock&7)==7 ? "on\n" : "off\n");
    console_write("VCC enable="); console_write(s.gpio_inputs&(1u<<4) ? "input" : s.gpio_levels&(1u<<4) ? "high" : "low");
    console_write(" IO voltage select="); console_write(s.gpio_inputs&(1u<<3) ? "input" : s.gpio_levels&(1u<<3) ? "1800 mV" : "3300 mV");
    console_write("\nSDHCI spec_code="); console_u64((s.version>>16)&255);
    console_write(" base_clock_MHz="); console_u64((s.capabilities>>8)&255);
    console_write(" supports_3v3="); console_write(s.capabilities&(1u<<24) ? "yes" : "no");
    console_write(" signaling_1v8="); console_write(s.host_control2&(1u<<19) ? "yes" : "no");
    console_write(" cqe="); console_write(s.cqe&1 ? "yes" : "no");
    console_write(" pin_select="); console_u64(s.pin_select&3); console_write("\n");
}

static bool card_present(void *context)
{
    uint32_t level;
    return probe_read(context,SD_GPIO_DATA,&level) && (level&SD_GPIO_MASK)==SD_GPIO_READY;
}

void pi5_sd_test(const void *dtb,unsigned count,bool tail)
{
    static unsigned ticket;
    if (ticket) { console_write("microSD host restore unverified. Reboot before retrying.\n"); return; }
    if (!pi5_usb_boot_selected(dtb,0x200000)) { console_write("sdtest requires USB boot.\n"); return; }
    struct pi5_sd_snapshot s;
    if (pi5_sd_snapshot(dtb,0x200000,probe_read,NULL,&s)!=BLOCK_OK) {
        console_write("sdtest requires recognized microSD registers and wiring.\n"); return;
    }
    if (s.gpio_levels&(1u<<5)) { console_write("sdtest: no card detected.\n"); return; }
    if ((s.gpio_inputs&0x18) || (s.gpio_levels&SD_GPIO_MASK)!=SD_GPIO_READY ||
        (s.control&0x1100) || (s.clock&0x07000005) || (s.present&3) || (s.cqe&1)) {
        console_write("sdtest requires an idle SD host and firmware-set 3.3 V card signals.\n"); return;
    }
    const struct resource_range claims[]={
        {RESOURCE_MMIO,0,SD_BASE,0x260},{RESOURCE_MMIO,0,SD_BASE+0x400,0x200}
    };
    ticket=resource_acquire("sdtest",claims,2);
    if (!ticket) { console_write("microSD host already claimed.\n"); return; }
    uint32_t pin=(s.pin_select&~3u)|2;
    if (pin!=s.pin_select) { write_reg(NULL,SD_PIN_SELECT,pin); }
    const struct sdhci_io io={read_reg,write_reg,ticks,NULL,timer_frequency()};
    struct sdhci_cold_result out={0};
    enum block_result result=(read_reg(NULL,SD_PIN_SELECT)&3)==2 ? sdhci_cold_test(&io,write_byte,card_present,count,tail,&out) : BLOCK_IO;
    if (pin!=s.pin_select) { write_reg(NULL,SD_PIN_SELECT,s.pin_select); }
    if ((read_reg(NULL,SD_PIN_SELECT)&3)!=(s.pin_select&3)) { out.restore_errors|=SD_RESTORE_PINS; }
    if (!out.restore_errors) {
        (void)resource_release(ticket); ticket=0;
    }
    if (result==BLOCK_OK && !ticket) {
        console_write("PASS: read-only microSD sectors="); console_u64(out.sectors);
        console_write(" lba="); console_u64(out.first_lba);
        console_write(" read="); console_u64(out.completed);
        console_write(" fnv1a32="); console_hex(out.hash); console_write("\n");
        return;
    }
    console_write("sdtest: "); console_write(block_error(ticket ? BLOCK_IO : result));
    console_write(" cmd="); console_u64(out.command);
    console_write(" irq="); console_hex(out.last_interrupt);
    console_write(" response="); console_hex(out.last_response);
    console_write(" read="); console_u64(out.completed);
    console_write(" restore_mask="); console_hex(out.restore_errors);
    if (out.restore_errors&SD_RESTORE_CONTROL) {
        console_write(" control0_before="); console_hex(out.control_before);
        console_write(" after="); console_hex(out.control_after);
    }
    console_write("\n");
}
void pi5_sd_info(void)
{
    const struct sdhci_io io={.read=read_reg};
    struct sdhci_card_state card=sdhci_card_status(&io);
    console_write("microSD present="); console_write(card.present ? "yes" : "no");
    console_write(" stable="); console_write(card.stable ? "yes" : "no");
    console_write(" powered="); console_write(card.powered ? "yes" : "no");
    console_write(" clocked="); console_write(card.clocked ? "yes" : "no");
    console_write(" busy="); console_write(card.busy ? "yes" : "no");
    console_write(" cqe="); console_write(card.cqe ? "yes\n" : "no\n");
}
bool platform_storage_attach(struct block_device *device,uintptr_t dtb)
{
    /* A warm reset can leave SDHCI looking clocked and populated. The
     * firmware's selected USB boot source takes precedence over those bits. */
    bool usb_boot=pi5_usb_boot_selected((const void *)dtb,0x200000);
    if (usb_boot || !(read_reg(NULL,0x24)&(1u<<16)) || !(read_reg(NULL,0x2c)&4) || !(read_reg(NULL,0x28)&(1u<<8))) {
        if (usb_boot) { console_write("Firmware boot source: USB mass storage.\n"); }
        usb_backend=true; return usb_storage_attach(device);
    }
    struct sdhci_io io={.read=read_reg,.write=write_reg,.ticks=ticks,.hz=timer_frequency()};
    return sdhci_attach(&host,&io,device);
}
enum block_result platform_storage_quiesce(void)
{
    /* The read-only SD firmware handoff uses synchronous PIO. */
    return xhci_storage_quiesce();
}
void platform_storage_info(void)
{
    if (usb_backend) { console_write("Storage backend: USB2 xHCI / SCSI BOT.\n"); usb_storage_info(); return; }
    console_write("Storage backend: SD firmware handoff, or USB2 BOT when SD is absent/unclocked.\n");
    console_write("Pi SDHCI base="); console_hex(SD_BASE);
    console_write(" present="); console_hex(read_reg(NULL,0x24));
    console_write("\ncontrol0="); console_hex(read_reg(NULL,0x28));
    console_write(" control1="); console_hex(read_reg(NULL,0x2c));
    console_write("\ncontrol2="); console_hex(read_reg(NULL,0x3c));
    console_write(" cqe_config="); console_hex(read_reg(NULL,0x208));
    console_write("\nlast_irq="); console_hex(host.last_interrupt);
    console_write(" last_response="); console_hex(host.last_response);
    console_write("\nSD handoff: "); console_write(host.ready ? "selected card responding" : block_error(host.error));
    console_write("\nRequires firmware-selected SDHC/SDXC; no full card initialization yet.\n");
    console_write("An empty/unclocked SD host is expected when booting only from USB.\n");
}
