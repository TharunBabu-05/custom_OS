#ifndef OS_WIFI_POWER_H
#define OS_WIFI_POWER_H
#include "block.h"
struct wifi_power_io {
    bool (*read32)(void *context,uint64_t address,uint32_t *value);
    bool (*write32)(void *context,uint64_t address,uint32_t value);
    uint64_t (*ticks)(void *context);
    void *context;
    uint64_t hz;
};
enum wifi_power_phase { WIFI_POWER_CHECK,WIFI_POWER_RESET,WIFI_POWER_PINS,
    WIFI_POWER_LOW,WIFI_POWER_VDD,WIFI_POWER_CLOCK,WIFI_POWER_READY };
struct wifi_power_state {
    enum wifi_power_phase phase;
    enum block_result error;
    bool attempted,ready;
    uint32_t stepping,caps,version,clock,source_hz,sd_hz;
};
bool pi5_wifi_power_description(const void *dtb,size_t available);
enum block_result pi5_wifi_power_up(const struct wifi_power_io *io,const void *dtb,size_t available,struct wifi_power_state *state);
#endif
