#ifndef OS_ETHERNET_IPV4_H
#define OS_ETHERNET_IPV4_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ethernet_ipv4 {
    uint8_t address[4],mask[4],prefix;
};

bool ethernet_ipv4_configure(struct ethernet_ipv4 *,const uint8_t address[4],unsigned prefix);
/* Returns the Ethernet frame length, or zero for a frame needing no reply. */
size_t ethernet_ipv4_reply(const struct ethernet_ipv4 *,const uint8_t mac[6],
                           const uint8_t *frame,size_t bytes,uint8_t *out,size_t capacity);
#endif
