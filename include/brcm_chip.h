#ifndef OS_BRCM_CHIP_H
#define OS_BRCM_CHIP_H
#include "block.h"
#define BRCM_MAX_CORES 16
#define BRCM_EROM_WORDS 1024
struct brcm_chip_io {
    enum block_result (*read32)(void *context,uint32_t address,uint32_t *value);
    enum block_result (*write32)(void *context,uint32_t address,uint32_t value);
    void *context;
};
struct brcm_core { uint16_t id; uint8_t revision; uint32_t base,wrapper; };
struct brcm_chip_info {
    bool attempted,cores_valid,ram_valid,bank_changed,bank_restored;
    enum block_result error;
    uint32_t erom,last_address,last_word,rom_words;
    uint32_t rom_sample[32]; /* First entries for diagnosing an unsupported live table. */
    unsigned core_count,banks;
    struct brcm_core core[BRCM_MAX_CORES];
    uint32_t arm_cap,arm_ioctl,arm_reset,ram_base,ram_bytes,bank_bytes[32];
};
/* BCM4345 rev6/AI only. No CPU/D11 reset or firmware upload. RAM sizing writes
 * only ARMCR4 BANKIDX and attempts to restore/verify its original value. */
enum block_result brcm_chip_discover(const struct brcm_chip_io *io,uint32_t chip_word,struct brcm_chip_info *info);
const char *brcm_core_name(uint16_t id);
#endif
