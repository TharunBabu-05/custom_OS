#ifndef OS_WIFI_H
#define OS_WIFI_H
#include <stdint.h>
void platform_wifi_probe(void);
void platform_wifi_init(uintptr_t dtb);
void platform_wifi_status(void);
void platform_wifi_cores(void);
void platform_wifi_load(void);
void platform_wifi_start(void);
void platform_wifi_control(void);
void platform_wifi_caps(void);
void platform_wifi_prepare(const char *country);
void platform_wifi_scan(void);
#endif
