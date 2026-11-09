#ifndef OS_BRCM_CONTROL_H
#define OS_BRCM_CONTROL_H
#include "brcm_start.h"
#define BRCM_CONTROL_FRAME_MAX 4096
struct brcm_control_io {
    void *context;
    enum block_result (*read8)(void *,unsigned,uint32_t,uint8_t *);
    enum block_result (*write8)(void *,unsigned,uint32_t,uint8_t);
    enum block_result (*read32)(void *,uint32_t,uint32_t *);
    enum block_result (*write32)(void *,uint32_t,uint32_t);
    enum block_result (*packet)(void *,bool,void *,size_t);
    enum block_result (*delay_us)(void *,unsigned);
};
enum brcm_control_phase { CONTROL_IDLE,CONTROL_CHECK,CONTROL_SETUP,CONTROL_F2,CONTROL_MAILBOX,CONTROL_CREDIT,CONTROL_ABI,CONTROL_VERSION,CONTROL_READY };
struct brcm_control_state {
    enum brcm_control_phase phase;enum block_result error;
    bool attempted,f2_ready,device_ready,ready,pending,waiting,replied,set_request,firmware_rejected;
    uint8_t clock_csr,enable,io_ready,tx_seq,tx_max,rx_seq;
    uint16_t block_size,request_id,frame_length;
    uint32_t last_address,last_word,interrupt,mailbox,command,firmware_status,ioctl_version;
    uint32_t last_header[3];
    unsigned polls,tx_frames,rx_frames,event_frames,data_frames,empty_frames,seq_resync,stale_replies;
    unsigned response_length,capacity;
    unsigned rx_limit;
    void (*event)(void *,const void *,size_t);
    void *event_context;
    unsigned char frame[BRCM_CONTROL_FRAME_MAX],response[1536];
    char version[256];
};
struct brcm_sdpcm_header { uint16_t length;uint8_t sequence,channel,offset,window; };
enum block_result brcm_sdpcm_header(const unsigned char *,size_t,struct brcm_sdpcm_header *);
/* One-shot F2 + GET_VERSION and GET_VAR("ver") only. No SET command,
 * scan/association, CLM, credentials or network data transmission. */
enum block_result brcm_control_start(const struct brcm_control_io *,const struct brcm_start_state *,struct brcm_control_state *);
/* Restricted IOVARs for capability/regulatory preparation only. The caller
 * provides input separately from output; output may be a truncated padded
 * firmware buffer, and *actual is always bounded by output capacity. */
enum block_result brcm_control_iovar(const struct brcm_control_io *,struct brcm_control_state *,bool set,const char *name,const void *input,size_t input_length,void *output,size_t capacity,size_t *actual,uint32_t *firmware_status);
enum block_result brcm_control_poll(const struct brcm_control_io *,struct brcm_control_state *);
/* Only interface UP/DOWN, not an arbitrary ioctl entry point. */
enum block_result brcm_control_radio(const struct brcm_control_io *,struct brcm_control_state *,bool up,uint32_t *status);
#endif
