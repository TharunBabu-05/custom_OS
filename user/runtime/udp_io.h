#ifndef OUROS_UDP_IO_H
#define OUROS_UDP_IO_H
#include "user.h"

/* Absolute uos_time_ms deadlines. Zero is a valid empty datagram length. */
int64_t udp_send_until(uint64_t socket,const void *data,size_t bytes,int64_t deadline);
int64_t udp_flush_until(uint64_t socket,int64_t deadline);
int64_t udp_recv_until(uint64_t socket,void *data,size_t capacity,int64_t deadline);
#endif
