#ifndef OS_BRCM_FIRMWARE_H
#define OS_BRCM_FIRMWARE_H
#include "brcm_chip.h"
#define BRCM_FW_ASSETS 3
struct brcm_fw_asset { const char *name,*sha256; uint32_t bytes; };
extern const struct brcm_fw_asset brcm_fw_assets[BRCM_FW_ASSETS];
struct brcm_fw_io {
    void *context;
    enum block_result (*open)(void *,unsigned asset,uint32_t *bytes);
    enum block_result (*file_read)(void *,uint32_t offset,void *data,size_t length);
    enum block_result (*read32)(void *,uint32_t address,uint32_t *value);
    enum block_result (*write32)(void *,uint32_t address,uint32_t value);
    enum block_result (*ram)(void *,bool write,uint32_t address,void *data,size_t length);
    enum block_result (*delay_us)(void *,unsigned microseconds);
    void (*progress)(void *,uint32_t verified,uint32_t total);
};
enum brcm_fw_phase { FW_IDLE,FW_FILES,FW_LAYOUT,FW_PASSIVE,FW_CODE,FW_NVRAM,FW_VERIFIED };
struct brcm_fw_state {
    enum brcm_fw_phase phase; enum block_result error;
    bool files_valid,attempted,cpu_halted,d11_disabled,token_cleared,complete;
    unsigned asset; uint32_t uploaded,verified,nvram_size,nvram_address,reset_vector,last_address;
    unsigned char digest[BRCM_FW_ASSETS][32];
    unsigned char source_nvram[4096],nvram[4096],buffer[512],check[512];
};
enum block_result brcm_fw_check_files(const struct brcm_fw_io *io,struct brcm_fw_state *state);
enum block_result brcm_nvram_pack(const void *text,size_t length,const unsigned char mac[6],void *output,size_t capacity,uint32_t *packed);
enum block_result brcm_fw_upload(const struct brcm_fw_io *io,uint32_t chip_word,const struct brcm_chip_info *cores,const unsigned char mac[6],struct brcm_fw_state *state);
bool brcm_fw_profile(uint32_t chip_word,const struct brcm_chip_info *cores);
#endif
