#ifndef OS_BRCM_SCAN_H
#define OS_BRCM_SCAN_H
#include "brcm_prepare.h"
#define BRCM_SCAN_AP_MAX 32
enum brcm_security { SCAN_PRIVACY=1,SCAN_WPA=2,SCAN_RSN=4,SCAN_PSK=8,SCAN_SAE=16,SCAN_OWE=32,SCAN_ENTERPRISE=64,SCAN_OTHER_AKM=128 };
struct brcm_scan_ap {
    unsigned char bssid[6],ssid[32];uint8_t ssid_length,channel,band;
    int16_t rssi;uint16_t chanspec,security;bool pmf_capable,pmf_required;
    uint32_t group_cipher,management_cipher;
    bool pairwise_ccmp,akm_sae; /* Exact 00:0f:ac:4 and :8; FT-SAE is distinct. */
    uint8_t rsnxe_length;unsigned char rsnxe[16];
};
struct brcm_scan_state {
    bool attempted,armed,request_accepted,complete,ready,interface_may_be_up,interface_down,mask_restored,truncated;
    enum block_result error,last_parse_error;unsigned phase,count,events,ignored,malformed,duplicates,overflow,polls;
    uint32_t firmware_status,event_status,event_reason,scan_version,escan_version,last_bss_version;
    uint16_t sync_id;unsigned char saved_mask[16];
    struct brcm_scan_ap ap[BRCM_SCAN_AP_MAX];
};
struct brcm_scan_io {
    void *context;
    enum block_result (*iovar)(void *,bool,const char *,const void *,size_t,void *,size_t,size_t *,uint32_t *);
    enum block_result (*radio)(void *,bool,uint32_t *);
    enum block_result (*poll)(void *);
    enum block_result (*delay_us)(void *,unsigned);
    uint64_t (*milliseconds)(void *);
};
/* Input is the complete channel-1 payload, including the 4-byte BCDC header. */
void brcm_scan_event(void *,const void *,size_t);
enum block_result brcm_scan_bss(const void *,size_t,struct brcm_scan_ap *);
enum block_result brcm_scan_run(const struct brcm_scan_io *,const struct brcm_prepare_state *,struct brcm_scan_state *);
#endif
