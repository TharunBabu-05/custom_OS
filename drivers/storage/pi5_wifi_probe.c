/* Diagnostic CMD5 query on the separate onboard SDIO host. This is not a
 * firmware loader, association driver or network interface. Never called at boot. */
#include "wifi.h"
#include "sdhci.h"
#include "console.h"
#include "cpu.h"
#include "sdio.h"
#define WIFI_HOST 0x1001100000UL
static struct sdio_card card={.error=BLOCK_NOT_READY};
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
static void host_status(void)
{
    console_write("Wi-Fi SDIO host="); console_hex(WIFI_HOST);
    console_write(" CONTROL0="); console_hex(rd(NULL,0x28)); console_write(" CONTROL1="); console_hex(rd(NULL,0x2c));
    console_write("\nPRESENT="); console_hex(rd(NULL,0x24)); console_write(" CAPS="); console_hex(rd(NULL,0x40));
    console_write(" CQE="); console_hex(rd(NULL,0x208)); console_write("\n");
}
void platform_wifi_status(void)
{
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
}
void platform_wifi_init(void)
{
    if (card.changed) { console_write("Wi-Fi SDIO init already attempted; reboot before retry.\n"); platform_wifi_status(); return; }
    console_write("Wi-Fi init: SDIO enumeration only; no RF association or network packets.\n");
    struct sdhci_io io={rd,wr,ticks,NULL,timer_frequency()};
    enum block_result r=sdio_initialize(&card,&io);
    console_write("SDIO initialization: "); console_write(block_error(r)); console_write("\n");
    if (r==BLOCK_OK) {
        unsigned char data[64]; size_t n=0x20000-card.cis; if (n>sizeof(data)) { n=sizeof(data); }
        r=sdio_read_bytes(&card,0,card.cis,data,n);
        console_write("CMD53 CIS read: "); console_write(block_error(r)); console_write(" bytes="); console_u64(n);
        console_write("\n");
    }
    host_status(); platform_wifi_status();
    console_write("WPA3-Personal requested; not connected. Needs board power/clock setup if unavailable,\n");
    console_write("chip/backplane access, firmware, FullMAC, SAE/PMF authentication and an IP stack.\n");
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
