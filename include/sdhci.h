#ifndef OS_SDHCI_H
#define OS_SDHCI_H
#include "block.h"
/* Injectable I/O permits the exact PIO command engine to run in host tests. */
struct sdhci_io {
    uint32_t (*read)(void *context,unsigned offset);
    void (*write)(void *context,unsigned offset,uint32_t value);
    uint64_t (*ticks)(void *context);
    void *context;
    uint64_t hz;
};
struct sdhci_host {
    struct sdhci_io io;
    uint32_t last_interrupt,last_response;
    enum block_result error;
    bool ready;
    bool (*card_present)(void *);
};
struct sdhci_card_state {
    bool present,stable,powered,clocked,busy,cqe;
};
struct sdhci_card_state sdhci_card_status(const struct sdhci_io *io);
struct pi5_sd_snapshot {
    uint32_t gpio_levels,gpio_inputs,present,control,clock,host_control2,capabilities,version,cqe,pin_select;
};
typedef bool (*pi5_sd_read32)(void *context,uint64_t address,uint32_t *value);
enum block_result pi5_sd_snapshot(const void *dtb,size_t bytes,pi5_sd_read32 read,
                                  void *context,struct pi5_sd_snapshot *out);
#ifdef OUROS_PI5
void pi5_sd_info(void);
void pi5_sd_probe(const void *dtb);
void pi5_sd_test(const void *dtb,unsigned count,bool tail);
#endif
enum { SDHCI_TEST_MAX_SECTORS=8 };
enum sdhci_restore_error {
    SD_RESTORE_CLOCK=1, SD_RESTORE_CONTROL=2, SD_RESTORE_STATUS=4, SD_RESTORE_SIGNAL=8,
    SD_RESTORE_BUSY=16, SD_RESTORE_PINS=32
};
struct sdhci_cold_result {
    uint64_t sectors,first_lba;
    uint32_t hash,last_interrupt,last_response,control_before,control_after;
    unsigned command,completed,restore_errors;
};
typedef void (*sdhci_write8)(void *,unsigned,uint8_t);
enum block_result sdhci_cold_test(const struct sdhci_io *io,sdhci_write8 write8,bool (*card_present)(void *),
                                  unsigned count,bool tail,struct sdhci_cold_result *out);
bool sdhci_attach(struct sdhci_host *host,const struct sdhci_io *io,struct block_device *device);
enum block_result sdhci_read_sector(void *context,uint64_t lba,unsigned char *sector);
/* CMD5 argument zero only: query a clocked SDIO device without negotiating OCR. */
enum block_result sdio_query(const struct sdhci_io *io,uint32_t *r4);
#endif
