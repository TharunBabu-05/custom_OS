#ifndef OS_BRCM_CONTROL_H
#define OS_BRCM_CONTROL_H
#include "brcm_start.h"
#include "netprobe.h"
#define BRCM_CONTROL_FRAME_MAX 4096
#define BRCM_GLOM_MAX 8192
#define BRCM_GLOM_FRAMES 16
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
    uint32_t station_mode;bool station_mode_known;
    uint32_t station_initial_mode,station_ap_mode,station_up_state;
    unsigned station_prepare_phase;
    bool station_initial_known,station_ap_known,station_up_known,station_change_attempted,station_mode_changed;
    unsigned polls,tx_frames,rx_frames,event_frames,data_frames,empty_frames,seq_resync,stale_replies;
    unsigned response_length,capacity;
    unsigned rx_limit;
    unsigned glom_descriptors,glom_frames,glom_subframes,glom_failures,glom_phase;
    unsigned glom_count,glom_last_count,glom_bad_index,glom_bytes,glom_wire_bytes;enum block_result glom_error;
    uint16_t glom_lengths[BRCM_GLOM_FRAMES];uint32_t glom_header[3],glom_subheader[3];
    unsigned char glom[BRCM_GLOM_MAX];
    void (*event)(void *,const void *,size_t);
    void *event_context;
    /* Optional synchronous raw BCDC data observer. No I/O from callbacks. */
    void (*data_observer)(void *,const void *,size_t);
    void *data_context;
    unsigned char frame[BRCM_CONTROL_FRAME_MAX],response[1536];
    char version[256];
};
struct brcm_sdpcm_header { uint16_t length;uint8_t sequence,channel,offset,window;bool descriptor; };
/* Public, peer-bound station metadata; statistics require SCBSTATS and length. */
struct brcm_station_info {
    unsigned version,length;uint32_t flags;unsigned char peer[6];bool valid,statistics;
    uint32_t tx_packets,tx_failures,rx_unicast,rx_multicast,decrypt_ok,decrypt_fail;
};
enum block_result brcm_station_info_decode(const void *,size_t,const unsigned char peer[6],struct brcm_station_info *);
enum block_result brcm_control_station_info(const struct brcm_control_io *,struct brcm_control_state *,const unsigned char peer[6],struct brcm_station_info *,uint32_t *);
enum block_result brcm_control_port(const struct brcm_control_io *,struct brcm_control_state *,const unsigned char peer[6],bool authorize,uint32_t *,bool (*permitted)(void *),void *);
enum block_result brcm_sdpcm_header(const unsigned char *,size_t,struct brcm_sdpcm_header *);
/* One-shot F2 + GET_VERSION and GET_VAR("ver") only. No SET command,
 * scan/association, CLM, credentials or network data transmission. */
enum block_result brcm_control_start(const struct brcm_control_io *,const struct brcm_start_state *,struct brcm_control_state *);
/* Restricted IOVARs for capability/regulatory/security/association stages. The caller
 * provides input separately from output; output may be a truncated padded
 * firmware buffer, and *actual is always bounded by output capacity. */
enum block_result brcm_control_iovar(const struct brcm_control_io *,struct brcm_control_state *,bool set,const char *name,const void *input,size_t input_length,void *output,size_t capacity,size_t *actual,uint32_t *firmware_status);
enum block_result brcm_control_poll(const struct brcm_control_io *,struct brcm_control_state *);
/* Initial SAE EAPOL M2 only, interface 0 / SDPCM data channel. Success is
 * SDIO write completion, not an air ACK. Never call from RX callbacks. */
enum block_result brcm_control_eapol_m2(const struct brcm_control_io *,struct brcm_control_state *,const void *ethernet,size_t,bool (*permitted)(void *),void *);
enum block_result brcm_control_eapol_m4(const struct brcm_control_io *,struct brcm_control_state *,const void *,size_t,bool (*permitted)(void *),void *);
enum block_result brcm_control_netprobe(const struct brcm_control_io *,struct brcm_control_state *,const struct netprobe *,enum netprobe_packet,unsigned,const void *,size_t,bool (*permitted)(void *),void *);
/* DHCP DISCOVER/REQUEST only, guarded after credit RX; no generic IP TX. */
enum block_result brcm_control_dhcp(const struct brcm_control_io *,struct brcm_control_state *,const void *,size_t,bool (*permitted)(void *),void *);
/* Typed, guarded wsec_key SET only. All shared payload/reply buffers erased. */
enum block_result brcm_control_key(const struct brcm_control_io *,struct brcm_control_state *,const void *,size_t,uint32_t *,bool (*permitted)(void *),void *);
/* Only interface UP/DOWN, not an arbitrary ioctl entry point. */
enum block_result brcm_control_radio(const struct brcm_control_io *,struct brcm_control_state *,bool up,uint32_t *status);
/* Read-only WLC_GET_INFRA, accepted only when firmware reports STA mode 1. */
enum block_result brcm_control_station(const struct brcm_control_io *,struct brcm_control_state *,uint32_t *status);
/* Normal client initialization: require AP=0/UP=0 and INFRA 0 or 1; select
 * INFRA=1 if needed and verify GET_INFRA. Selection persists for this boot.
 * No radio UP, join, password, key or arbitrary command interface. */
enum block_result brcm_control_station_prepare(const struct brcm_control_io *,struct brcm_control_state *,uint32_t *status);
struct brcm_link_identity { unsigned char bssid[6],ssid[32];uint32_t ssid_length; };
/* Read-only GET_BSSID + GET_SSID; association still requires independent
 * successful SAE/firmware events and policy checks by the caller. */
enum block_result brcm_control_identity(const struct brcm_control_io *,struct brcm_control_state *,struct brcm_link_identity *,uint32_t *status);
#endif
