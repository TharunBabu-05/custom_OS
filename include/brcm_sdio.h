#ifndef OS_BRCM_SDIO_H
#define OS_BRCM_SDIO_H
#include "sdio.h"
#define BRCM_F1_BLOCK_SIZE 64
struct brcm_sdio_state {
    unsigned phase;
    enum block_result error;
    bool function1_ready,chip_read;
    uint8_t clock_csr;
    uint32_t chip_id_word;
    uint16_t function1_block_size;
};
/* Enables only function 1, requests ALP and reads ChipCommon ID. No firmware/RF. */
enum block_result brcm_sdio_identify(struct sdio_card *card,struct brcm_sdio_state *state);
/* Aligned core/EROM register accesses, gated by function-1/chip identification.
 * No chip RAM upload or RF data path. */
enum block_result brcm_sdio_read32(struct sdio_card *card,const struct brcm_sdio_state *state,uint32_t address,uint32_t *value);
enum block_result brcm_sdio_write32(struct sdio_card *card,const struct brcm_sdio_state *state,uint32_t address,uint32_t value);
/* Only measured 800 KiB TCM at 0x198000; caller separately verifies CPU halt.
 * Aligned 4..512-byte calls, split into <=64-byte wire requests and at windows. */
enum block_result brcm_sdio_ram(struct sdio_card *card,const struct brcm_sdio_state *state,bool write,uint32_t address,void *data,size_t length);
/* Single reset-vector word at the CR4 address-zero alias, with readback.
 * Caller must gate on verified upload and a still-halted CR4. */
enum block_result brcm_sdio_reset_vector(struct sdio_card *,const struct brcm_sdio_state *,uint32_t value);
/* Packet channel only; caller separately gates on firmware startup/F2 ready.
 * TX <=2048 bytes, split into <=512-byte incrementing CMD53 requests whose
 * addresses continue across chunks; RX <=512 bytes at the fixed frame port. */
enum block_result brcm_sdio_packet(struct sdio_card *,const struct brcm_sdio_state *,bool write,void *,size_t);
#endif
