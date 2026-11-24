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

static bool prefix(const char *text,const char *name)
{
    while (*name) { if (*text++!=*name++) { return false; } }
    return true;
}

static bool firmware_property(uintptr_t base,enum pi5_property property,const char *date,uint32_t *value)
{
    if (!base) { console_write("Firmware query: unavailable (device-tree resource absent).\n"); return false; }
    uint64_t seconds=0;
    if (date && (!utc_parse(date,&seconds) || seconds<UTC_FIRST_SET || seconds>UINT32_MAX)) {
        console_write("Usage: rtcset <YYYY-MM-DDTHH:MM:SSZ> (2000 through 2106-02-07T06:28:15Z).\n");
        return false;
    }
    uintptr_t address=(uintptr_t)message;
    if (address>0x40000000-sizeof(message) || !mmu_dma_buffer(address,sizeof(message))) {
        console_write("Firmware query: DMA buffer unavailable.\n");
        return false;
    }
    /* Raspberry Pi firmware's uncached VideoCore alias for low RAM. */
    mailbox.bus_address=(uint32_t)address|0xc0000000;
    static const struct pi5_mailbox_io io={read_register,write_register,timer_ticks,sync_io};
    uint64_t timeout=timer_frequency()/2;
    enum pi5_reply reply=date ? pi5_rtc_set_time(&mailbox,&io,base,timeout,(uint32_t)seconds,value) :
                                pi5_property_read(&mailbox,&io,base,timeout,property,value);
    if (reply!=PI5_REPLY_OK) {
        static const char *const failure[]={
            [PI5_REPLY_BUSY]="mailbox busy",
            [PI5_REPLY_TIMEOUT]="timed out; disabled until reboot",
            [PI5_REPLY_MISMATCH]="unexpected response; disabled until reboot",
            [PI5_REPLY_REJECTED]="tag refused or unsupported",
            [PI5_REPLY_INVALID]="invalid request or response",
            [PI5_REPLY_BLOCKED]="disabled until reboot"
        };
        console_write(date ? "RTC set unverified: " : "Firmware query: ");
        console_write(failure[reply]); console_write(".\n");
        if (reply==PI5_REPLY_INVALID && property>=PI5_RTC_ALARM) {
            console_write("RTC alarm register="); console_u64(property==PI5_RTC_ALARM ? 1 : 3);
            console_write(" reply_bytes="); console_u64(mailbox.reply_length);
            console_write(" selector="); console_hex(mailbox.reply_selector);
            console_write(" value="); console_hex(mailbox.reply_value); console_write("\n");
        }
        return false;
    }
    return true;
}

static void show_power(uint32_t flags)
{
    static const char *const names[]={" undervoltage"," frequency_capped"," throttled"," soft_limit"};
    console_write("Firmware throttle_flags="); console_hex(flags);
    for (unsigned i=0;i<4;++i) {
        console_write(names[i]); console_write(flags&(1u<<i) ? "_now=yes" : "_now=no");
        console_write(names[i]); console_write(flags&(1u<<(i+16)) ? "_seen=yes" : "_seen=no");
    }
    console_write("\n");
}

static void show_rtc(const char *label,uint32_t value)
{
    char date[UTC_TEXT_BYTES];
    (void)utc_format(value,date);
    console_write(label); console_u64(value); console_write(" "); console_write(date);
    console_write("\n");
}

bool platform_health_command(const char *line, const void *dtb, bool pi5)
{
    bool inventory=equal(line,"platforminfo");
    bool thermal=equal(line,"therminfo");
    bool power=equal(line,"powerinfo");
    bool rtc=equal(line,"rtcinfo"),sync=equal(line,"rtcsync"),alarm=equal(line,"rtcalarm");
    bool set=equal(line,"rtcset") || prefix(line,"rtcset ");
    if (!inventory && !thermal && !power && !rtc && !sync && !alarm && !set) { return false; }
    if (!pi5) { console_write("Platform health: requires Raspberry Pi 5.\n"); return true; }

    struct pi5_health resources=pi5_health_discover(dtb,0x200000);
    if (inventory) {
        show_source("Pi 5 AVS thermal register: ",resources.thermal);
        show_source("Pi 5 firmware mailbox: ",resources.mailbox);
        console_write(resources.rtc ? "Pi 5 RTC: described by device tree.\n" : "Pi 5 RTC: unavailable.\n");
    } else if (thermal) {
        show_temperature(resources.thermal);
    } else {
        uintptr_t base=power || resources.rtc ? resources.mailbox : 0;
        enum pi5_property property=power ? PI5_THROTTLED : alarm ? PI5_RTC_ALARM_ENABLE : PI5_RTC_TIME;
        uint32_t value,enabled;
        const char *date=set ? (line[6] ? line+7 : "") : NULL;
        if (!firmware_property(base,property,date,&value)) { return true; }
        if (power) { show_power(value); }
        else if (sync) {
            if (value<UTC_FIRST_SET) { console_write("RTC time must be set before UTC sync.\n"); }
            else { (void)system_utc_set(value,true); }
        } else if (alarm) {
            if (firmware_property(base,PI5_RTC_ALARM,NULL,&enabled)) {
                show_rtc(value ? "RTC alarm enabled unix_seconds=" : "RTC alarm disabled unix_seconds=",enabled);
            } else {
                console_write(value ? "RTC alarm enabled, timestamp unavailable.\n" :
                                      "RTC alarm disabled, timestamp unavailable.\n");
            }
        } else {
            show_rtc(set ? "RTC set verified unix_seconds=" : "RTC unix_seconds=",value);
        }
    }
    return true;
}
