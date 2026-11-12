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
void platform_wifi_target(const char *ssid);
void platform_wifi_auth_prepare(const char *ssid);
void platform_wifi_join_probe(const char *ssid);
void platform_wifi_sae_probe(const char *ssid);
void platform_wifi_sae_auth(const char *ssid);
void platform_wifi_assoc(const char *ssid);
void platform_wifi_dhcp(const char *ssid);
void platform_wifi_ping(const char *ssid);
void platform_wifi_connect(const char *ssid);
void platform_network_info(void);
#endif
