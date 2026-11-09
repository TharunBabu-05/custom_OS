#ifndef OS_BRCM_START_H
#define OS_BRCM_START_H
#include "brcm_firmware.h"
struct brcm_start_io {
    void *context;
    enum block_result (*read32)(void *,uint32_t,uint32_t *);
    enum block_result (*write32)(void *,uint32_t,uint32_t);
    enum block_result (*read_ram)(void *,uint32_t,void *,size_t);
    /* Dedicated address-zero reset-vector write AND readback. */
    enum block_result (*vector)(void *,uint32_t);
    enum block_result (*clock)(void *,bool write,uint8_t *);
    enum block_result (*delay_us)(void *,unsigned);
};
enum brcm_start_phase { START_IDLE,START_CHECK,START_VECTOR,START_RESET,START_RELEASE,START_CLOCK,START_SHARED,START_READY };
struct brcm_start_state {
    enum brcm_start_phase phase;
    enum block_result error;
    bool attempted,may_run,ready;
    uint8_t clock_csr;
    uint32_t last_address,last_word,reset_vector,token,shared,flags,console,trap,assert_line,mailbox;
    unsigned polls;
};
/* One-shot CPU startup + bounded HT/shared-memory checks; no function 2,
 * FullMAC requests, scan, credentials or IP traffic. Failure after release
 * does not imply a halted radio CPU. Reboot rather than retrying. */
enum block_result brcm_fw_start(const struct brcm_start_io *,uint32_t chip,
    const struct brcm_chip_info *,struct brcm_fw_state *,struct brcm_start_state *);
#endif
