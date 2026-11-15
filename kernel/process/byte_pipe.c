#include "byte_pipe.h"
void byte_pipe_init(struct byte_pipe *p)
{
    if (!p) { return; }for (size_t i=0;i<BYTE_PIPE_CAPACITY;++i) { p->bytes[i]=0; }
    p->head=p->count=0;p->reader=p->writer=true;
}
void byte_pipe_close_reader(struct byte_pipe *p)
{
    if (!p) { return; }p->reader=false;p->head=p->count=0;
    for (size_t i=0;i<BYTE_PIPE_CAPACITY;++i) { p->bytes[i]=0; }
}
void byte_pipe_close_writer(struct byte_pipe *p) { if (p) { p->writer=false; } }
enum byte_pipe_result byte_pipe_write(struct byte_pipe *p,const void *buffer,size_t bytes)
{
    if (!p || (!buffer && bytes) || bytes>BYTE_PIPE_CAPACITY) { return BYTE_PIPE_ARGUMENT; }
    if (!bytes) { return BYTE_PIPE_OK; }
    if (!p->reader || !p->writer) { return BYTE_PIPE_BROKEN; }
    if (bytes>BYTE_PIPE_CAPACITY-p->count) { return BYTE_PIPE_AGAIN; }
    const unsigned char *s=buffer;
    for (size_t i=0;i<bytes;++i) { p->bytes[(p->head+p->count+i)%BYTE_PIPE_CAPACITY]=s[i]; }
    p->count+=bytes;return BYTE_PIPE_OK;
}
enum byte_pipe_result byte_pipe_read(struct byte_pipe *p,void *buffer,size_t capacity,size_t *count)
{
    if (count) { *count=0; }
    if (!p || !count || (!buffer && capacity)) { return BYTE_PIPE_ARGUMENT; }
    if (!capacity) { return BYTE_PIPE_OK; }if (!p->reader) { return BYTE_PIPE_BROKEN; }
    if (!p->count) { return p->writer ? BYTE_PIPE_AGAIN : BYTE_PIPE_OK; }
    size_t n=capacity<p->count ? capacity : p->count;unsigned char *out=buffer;
    for (size_t i=0;i<n;++i) { out[i]=p->bytes[p->head];p->bytes[p->head]=0;p->head=(p->head+1)%BYTE_PIPE_CAPACITY; }
    p->count-=n;*count=n;return BYTE_PIPE_OK;
}
