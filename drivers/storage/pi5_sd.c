#include "sdhci.h"
#include "console.h"
#include "cpu.h"
/* Pi DT mmc0 /soc@107c000000/mmc@fff000 translated via soc ranges. */
#define SD_BASE 0x1000fff000UL
static struct sdhci_host host={.error=BLOCK_NOT_READY};
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
static uint64_t ticks(void *context) { (void)context; return timer_ticks(); }
bool platform_storage_attach(struct block_device *device)
{
    struct sdhci_io io={.read=read_reg,.write=write_reg,.ticks=ticks,.hz=timer_frequency()};
    return sdhci_attach(&host,&io,device);
}
void platform_storage_info(void)
{
    console_write("Storage backend: microSD SDHCI only; USB pendrives are not supported yet.\n");
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
