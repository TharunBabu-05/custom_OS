#ifndef OUROS_TCP_IO_H
#define OUROS_TCP_IO_H
#include "user.h"

/* Deadlines use the absolute clock returned by uos_time_ms(). */
int64_t tcp_wait_connected(uint64_t socket,int64_t deadline);
int64_t tcp_read_until(uint64_t socket,void *buffer,size_t bytes,int64_t deadline);
int64_t tcp_write_until(uint64_t socket,const void *buffer,size_t bytes,int64_t deadline);
/* Read-exact and write-all share the caller's deadline across all chunks.
 * They return zero on success or a UOS error. UOS_EOF means a short read. */
int64_t tcp_read_exact(uint64_t socket,void *buffer,size_t bytes,int64_t deadline);
int64_t tcp_write_all(uint64_t socket,const void *buffer,size_t bytes,int64_t deadline);
#endif
