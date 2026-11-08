/* Diagnostic CMD5 query on the separate onboard SDIO host. This is not a
 * firmware loader, association driver or network interface. Never called at boot. */
#include "wifi.h"
#include "sdhci.h"
#include "console.h"
#include "cpu.h"
#define WIFI_HOST 0x1001100000UL
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
void platform_wifi_probe(void)
{
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
    console_write("\nQuery only; Wi-Fi power/reset, CMD52/53, firmware and networking are not initialized.\n");
}
