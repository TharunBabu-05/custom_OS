#ifndef OS_SDIO_H
#define OS_SDIO_H
#include "sdhci.h"
enum sdio_phase { SDIO_PREFLIGHT,SDIO_IDLE,SDIO_OCR,SDIO_RCA,SDIO_SELECT,SDIO_CCCR,SDIO_CIS,SDIO_READY };
enum sdio_transfer_stage { SDIO_COMMAND,SDIO_BUFFER,SDIO_PIO,SDIO_COMPLETE,SDIO_FINISHED };
struct sdio_failure {
    bool valid; enum block_result error; enum sdio_transfer_stage stage;
    uint32_t command,argument,response,interrupt,present,blocks,control;
    unsigned bytes,fifo_bytes;
};
struct sdio_card {
    struct sdhci_io io;
    enum sdio_phase phase;
    enum block_result error;
    uint32_t r4,last_interrupt,last_response,cis;
    uint16_t rca,vendor,device;
    uint8_t functions,cccr_revision,sdio_revision,bus_control;
    bool changed,selected,have_identity;
    uint32_t last_command,last_argument;
    enum sdio_transfer_stage transfer_stage;
    unsigned transfer_bytes,fifo_bytes;
    struct sdio_failure failure; /* First failure, retained across cleanup/refusal. */
};
/* Requires a powered, clocked, idle host. No board power/clock/pinmux setup. */
enum block_result sdio_initialize(struct sdio_card *card,const struct sdhci_io *io);
enum block_result sdio_read_byte(struct sdio_card *card,unsigned function,uint32_t address,uint8_t *value);
enum block_result sdio_write_byte(struct sdio_card *card,unsigned function,uint32_t address,uint8_t value);
/* One aligned function-1 word, byte-mode CMD53/PIO. No DMA or function-2 data. */
enum block_result sdio_write_word(struct sdio_card *card,uint32_t address,uint32_t value);
enum block_result sdio_write_words(struct sdio_card *card,uint32_t address,const void *data,size_t length);
/* F2 frame port at 0x8000: fixed-address reads, incrementing writes as in
 * brcmfmac. Caller enables F2 and sets the ChipCommon window. 4..512 bytes. */
enum block_result sdio_f2_packet(struct sdio_card *,bool write,void *,size_t);
enum block_result sdio_f2_packet_at(struct sdio_card *,bool write,uint32_t address,void *,size_t);
/* Common function via CMD52; other functions via byte-mode incrementing CMD53.
 * At most 512 bytes per call, no data writes or DMA. */
enum block_result sdio_read_bytes(struct sdio_card *card,unsigned function,uint32_t address,void *data,size_t length);
#endif
