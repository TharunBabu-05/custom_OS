#ifndef OS_UDP_H
#define OS_UDP_H
#include "block.h"
#include "user_abi.h"

enum { UDP_SOCKET_LIMIT=4,UDP_OWNER_LIMIT=2,UDP_FRAME_MAX=42+UOS_UDP_MAX_BYTES };
/* Internal transmit request. A null peer MAC selects a directed ARP query.
 * This view is borrowed only for the synchronous transmit callback. */
struct udp_packet {
    const unsigned char *local_mac,*local_ip,*peer_mac,*peer_ip;
    uint16_t local_port,peer_port;
    const void *payload;size_t bytes;
};
bool udp_encode(const struct udp_packet *,void *,size_t,size_t *);
typedef enum block_result (*udp_transmit)(void *,const struct udp_packet *);

/* The cooperative worker owns the link. EL0 syscalls only operate on the
 * queues under the kernel root. No path yields or calls a driver in an IRQ. */
bool udp_link_up(const unsigned char mac[6],const unsigned char ip[4],const unsigned char mask[4]);
void udp_link_down(void);
void udp_receive(const void *,size_t,uint64_t now_ms);
enum block_result udp_service(udp_transmit,void *,uint64_t now_ms);
int64_t udp_open(uint64_t owner,const struct uos_udp_peer *);
int64_t udp_send(uint64_t owner,uint64_t handle,const void *,size_t,uint64_t now_ms);
int64_t udp_recv(uint64_t owner,uint64_t handle,void *,size_t);
int64_t udp_close(uint64_t owner,uint64_t handle);
void udp_close_owner(uint64_t owner);
unsigned udp_open_count(void);
#endif
