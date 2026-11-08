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
};
bool sdhci_attach(struct sdhci_host *host,const struct sdhci_io *io,struct block_device *device);
enum block_result sdhci_read_sector(void *context,uint64_t lba,unsigned char *sector);
/* CMD5 argument zero only: query a clocked SDIO device without negotiating OCR. */
enum block_result sdio_query(const struct sdhci_io *io,uint32_t *r4);
#endif
