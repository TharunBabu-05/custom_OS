#ifndef OS_BRCM_PREPARE_H
#define OS_BRCM_PREPARE_H
#include "block.h"
struct brcm_prepare_io {
    void *context;
    enum block_result (*iovar)(void *,bool set,const char *,const void *,size_t,void *,size_t,size_t *,uint32_t *);
    enum block_result (*open)(void *,unsigned,uint32_t *);
    enum block_result (*read)(void *,uint32_t,void *,size_t);
};
struct brcm_caps_state {
    bool attempted,ready,sae,external_sae,mfp_known,mfp_supported,supplicant_known,supplicant_supported;
    enum block_result error;unsigned phase;uint32_t firmware_status,mfp_value,supplicant_value,mfp_status,supplicant_status;
    unsigned char mac[6];char caps[768];
};
struct brcm_prepare_state {
    bool attempted,files_verified,clm_status_valid,clm_complete,country_written,ready;
    enum block_result error;unsigned phase,chunks;uint32_t uploaded,firmware_status,clm_status,country_revision;
    char requested[3],country[3],ccode[3],clm_version[256];
    unsigned char blob[2676],chunk[1412];
    unsigned char country_reply[12];
};
enum block_result brcm_caps_read(const struct brcm_prepare_io *,struct brcm_caps_state *);
enum block_result brcm_wifi_prepare(const struct brcm_prepare_io *,const char *country,struct brcm_prepare_state *);
bool brcm_country_valid(const char *);
#endif
