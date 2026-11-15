#ifndef OS_BYTE_PIPE_H
#define OS_BYTE_PIPE_H
#include <stdbool.h>
#include <stddef.h>
enum { BYTE_PIPE_CAPACITY=1024 };
enum byte_pipe_result { BYTE_PIPE_OK,BYTE_PIPE_AGAIN,BYTE_PIPE_BROKEN,BYTE_PIPE_ARGUMENT };
struct byte_pipe { unsigned char bytes[BYTE_PIPE_CAPACITY];size_t head,count;bool reader,writer; };
/* Serialized kernel ring. Writes are all-or-nothing; reads drain buffered data
 * before EOF. The scheduler owns blocking/wakeup and endpoint lifetimes. */
void byte_pipe_init(struct byte_pipe *);
void byte_pipe_close_reader(struct byte_pipe *);
void byte_pipe_close_writer(struct byte_pipe *);
enum byte_pipe_result byte_pipe_write(struct byte_pipe *,const void *,size_t);
enum byte_pipe_result byte_pipe_read(struct byte_pipe *,void *,size_t,size_t *);
#endif
