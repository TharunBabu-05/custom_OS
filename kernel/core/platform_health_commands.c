#include "console.h"
#include "cpu.h"
#include "mmu.h"
#include "platform_health.h"
#include "trust_time.h"

static _Alignas(16) volatile uint32_t message[PI5_MESSAGE_WORDS];
static struct pi5_mailbox mailbox={.message=message};

static bool equal(const char *a, const char *b)
{
    while (*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}

static uint32_t read_register(uintptr_t base, unsigned offset)
{
    return *(volatile uint32_t *)(base+offset);
}

static void write_register(uintptr_t base, unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(base+offset)=value;
}

static void sync_io(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static void show_source(const char *name, uintptr_t address)
{
    console_write(name);
    if (address) { console_hex(address); }
    else { console_write("unavailable"); }
    console_write("\n");
}

static void show_temperature(uintptr_t address)
{
    int32_t temperature;
    if (!address || !pi5_temperature(read_register(address,0),&temperature)) {
        console_write("SoC temperature: unavailable (sensor absent or sample invalid).\n");
        return;
    }
    console_write("SoC temperature_mC=");
    if (temperature<0) { console_write("-"); temperature=-temperature; }
    console_u64((uint32_t)temperature);
    console_write("\n");
}

static void show_property(uintptr_t base, enum pi5_property property)
{
    if (!base) { console_write("Firmware query: unavailable (device-tree resource absent).\n"); return; }
    uintptr_t address=(uintptr_t)message;
    if (address>0x40000000-sizeof(message) || !mmu_dma_buffer(address,sizeof(message))) {
        console_write("Firmware query: DMA buffer unavailable.\n");
        return;
    }
    /* Raspberry Pi firmware's uncached VideoCore alias for low RAM. */
    mailbox.bus_address=(uint32_t)address|0xc0000000;
    static const struct pi5_mailbox_io io={read_register,write_register,timer_ticks,sync_io};
    uint32_t value;
    enum pi5_reply reply=pi5_property_read(&mailbox,&io,base,timer_frequency()/2,property,&value);
    if (reply!=PI5_REPLY_OK) {
        static const char *const failure[]={
            [PI5_REPLY_BUSY]="mailbox busy",
            [PI5_REPLY_TIMEOUT]="timed out; disabled until reboot",
            [PI5_REPLY_MISMATCH]="unexpected response; disabled until reboot",
            [PI5_REPLY_REJECTED]="tag refused or unsupported",
            [PI5_REPLY_INVALID]="invalid request or response",
            [PI5_REPLY_BLOCKED]="disabled until reboot"
        };
        console_write("Firmware query: "); console_write(failure[reply]); console_write(".\n");
        return;
    }
    if (property==PI5_THROTTLED) {
        console_write("Firmware throttle_flags="); console_hex(value);
        console_write(value&1 ? " undervoltage_now=yes" : " undervoltage_now=no");
        console_write(value&(1u<<16) ? " undervoltage_seen=yes\n" : " undervoltage_seen=no\n");
        return;
    }
    char date[UTC_TEXT_BYTES];
    console_write("RTC unix_seconds="); console_u64(value); console_write(" ");
    console_write(utc_format(value,date) ? date : "(outside supported UTC date range)");
    console_write("\n");
}

bool platform_health_command(const char *line, const void *dtb, bool pi5)
{
    bool inventory=equal(line,"platforminfo");
    bool thermal=equal(line,"therminfo");
    bool power=equal(line,"powerinfo");
    bool rtc=equal(line,"rtcinfo");
    if (!inventory && !thermal && !power && !rtc) { return false; }
    if (!pi5) { console_write("Platform health: requires Raspberry Pi 5.\n"); return true; }

    struct pi5_health resources=pi5_health_discover(dtb,0x200000);
    if (inventory) {
        show_source("Pi 5 AVS thermal register: ",resources.thermal);
        show_source("Pi 5 firmware mailbox: ",resources.mailbox);
        console_write(resources.rtc ? "Pi 5 RTC: described by device tree.\n" : "Pi 5 RTC: unavailable.\n");
    } else if (thermal) {
        show_temperature(resources.thermal);
    } else {
        show_property(rtc && !resources.rtc ? 0 : resources.mailbox,
                      power ? PI5_THROTTLED : PI5_RTC_TIME);
    }
    return true;
}
