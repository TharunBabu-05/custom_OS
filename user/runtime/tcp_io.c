#include "tcp_io.h"

enum tcp_operation { CONNECT,READ,WRITE };

static int64_t wait_io(uint64_t socket,enum tcp_operation op,void *in,const void *out,
                       size_t bytes,int64_t deadline)
{
    for (;;) {
        int64_t now=uos_time_ms();
        if (now<0) { return now; }
        if (now>=deadline) { return UOS_ETIMEDOUT; }
        int64_t result;
        switch (op) {
        case CONNECT:result=uos_tcp_status(socket);break;
        case READ:result=uos_tcp_read(socket,in,bytes);break;
        default:result=uos_tcp_write(socket,out,bytes);break;
        }
        if (result!=UOS_EAGAIN) { return result; }

        now=uos_time_ms();
        if (now<0) { return now; }
        if (now>=deadline) { return UOS_ETIMEDOUT; }
        int64_t left=deadline-now;
        unsigned slice=left>UOS_WAIT_MAX_MS ? UOS_WAIT_MAX_MS : (unsigned)left;
        result=uos_socket_wait(UOS_EVENT_TCP,socket,op==READ ? UOS_EVENT_READ : UOS_EVENT_WRITE,slice);
        if (result<0) { return result; }
    }
}

int64_t tcp_wait_connected(uint64_t socket,int64_t deadline)
{ return wait_io(socket,CONNECT,NULL,NULL,0,deadline); }

int64_t tcp_read_until(uint64_t socket,void *buffer,size_t bytes,int64_t deadline)
{ return wait_io(socket,READ,buffer,NULL,bytes,deadline); }

int64_t tcp_write_until(uint64_t socket,const void *buffer,size_t bytes,int64_t deadline)
{ return wait_io(socket,WRITE,NULL,buffer,bytes,deadline); }

static int64_t transfer(uint64_t socket,void *in,const void *out,size_t bytes,int64_t deadline,bool reading)
{
    if (bytes && (reading ? !in : !out)) { return UOS_EFAULT; }
    size_t offset=0;
    while (offset<bytes) {
        size_t chunk=bytes-offset;
        if (chunk>UOS_TCP_MAX_BYTES) { chunk=UOS_TCP_MAX_BYTES; }
        int64_t n=reading ? tcp_read_until(socket,(unsigned char *)in+offset,chunk,deadline) :
                            tcp_write_until(socket,(const unsigned char *)out+offset,chunk,deadline);
        if (n<0) { return n; }
        if (!n) { return reading ? UOS_EOF : UOS_ERROR; }
        if ((uint64_t)n>chunk) { return UOS_ERROR; }
        offset+=(size_t)n;
    }
    return 0;
}

int64_t tcp_read_exact(uint64_t socket,void *buffer,size_t bytes,int64_t deadline)
{ return transfer(socket,buffer,NULL,bytes,deadline,true); }

int64_t tcp_write_all(uint64_t socket,const void *buffer,size_t bytes,int64_t deadline)
{ return transfer(socket,NULL,buffer,bytes,deadline,false); }
