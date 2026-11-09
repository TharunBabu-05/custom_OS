#ifndef OS_SDIO_H
#define OS_SDIO_H
#include "sdhci.h"
enum sdio_phase { SDIO_PREFLIGHT,SDIO_IDLE,SDIO_OCR,SDIO_RCA,SDIO_SELECT,SDIO_CCCR,SDIO_CIS,SDIO_READY };
struct sdio_card {
    struct sdhci_io io;
    enum sdio_phase phase;
    enum block_result error;
    uint32_t r4,last_interrupt,last_response,cis;
    uint16_t rca,vendor,device;
    uint8_t functions,cccr_revision,sdio_revision,bus_control;
    bool changed,selected,have_identity;
};
/* Requires a powered, clocked, idle host. No board power/clock/pinmux setup. */
enum block_result sdio_initialize(struct sdio_card *card,const struct sdhci_io *io);
enum block_result sdio_read_byte(struct sdio_card *card,unsigned function,uint32_t address,uint8_t *value);
/* CMD53 byte-mode, incrementing-address reads only; no writes or DMA. */
enum block_result sdio_read_bytes(struct sdio_card *card,unsigned function,uint32_t address,void *data,size_t length);
#endif
