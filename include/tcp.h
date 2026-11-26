#ifndef OS_TCP_H
#define OS_TCP_H
#include "block.h"
#include "user_abi.h"

enum { TCP_FIN=1,TCP_SYN=2,TCP_RST=4,TCP_PSH=8,TCP_ACK=16,TCP_FRAME_MAX=54+UOS_TCP_MAX_BYTES };
struct tcp_packet {
    const unsigned char *local_mac,*local_ip,*peer_mac,*peer_ip;
    uint32_t seq,ack;
    uint16_t local_port,peer_port,window;
    unsigned flags;
    const void *payload;size_t bytes;
};
bool tcp_encode(const struct tcp_packet *,void *,size_t,size_t *);
bool tcp_packet_pending(const struct tcp_packet *);
typedef enum block_result (*tcp_transmit)(void *,const struct tcp_packet *);

/* Same single-core cooperative ownership as UDP. A null peer MAC is ARP. */
bool tcp_link_up(const unsigned char mac[6],const unsigned char ip[4],const unsigned char mask[4],const unsigned char secret[32]);
void tcp_link_down(void);
void tcp_receive(const void *,size_t);
enum block_result tcp_service(tcp_transmit,void *,uint64_t now);
int64_t tcp_open(uint64_t owner,const struct uos_tcp_peer *,uint64_t now);
int64_t tcp_status(uint64_t owner,uint64_t handle);
int64_t tcp_write(uint64_t owner,uint64_t handle,const void *,size_t);
int64_t tcp_read(uint64_t owner,uint64_t handle,void *,size_t);
int64_t tcp_shutdown_write(uint64_t owner,uint64_t handle);
unsigned tcp_ready(uint64_t owner,uint64_t handle);
int64_t tcp_close(uint64_t owner,uint64_t handle,uint64_t now);
void tcp_close_owner(uint64_t owner,uint64_t now);
unsigned tcp_open_count(void);
#endif
