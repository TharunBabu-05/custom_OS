#include "udp_io.h"

static int64_t wait_ready(uint64_t socket,unsigned events,int64_t deadline)
{
    for (;;) {
        int64_t now=uos_time_ms();
        if (now<0) { return now; }
        if (now>=deadline) { return UOS_ETIMEDOUT; }
        int64_t left=deadline-now;
        unsigned wait=left>UOS_WAIT_MAX_MS ? UOS_WAIT_MAX_MS : (unsigned)left;
        int64_t ready=uos_socket_wait(UOS_EVENT_UDP,socket,events,wait);
        if (ready) { return ready; }
    }
}

int64_t udp_flush_until(uint64_t socket,int64_t deadline)
{
    int64_t ready=wait_ready(socket,UOS_EVENT_WRITE,deadline);
    if (ready<0) { return ready; }
    if (ready&UOS_EVENT_HANGUP) { return UOS_ENETDOWN; }
    if (ready&UOS_EVENT_ERROR) {
        struct uos_socket_info info;
        int64_t result=uos_socket_info(UOS_EVENT_UDP,socket,&info);
        return result ? result : info.error ? info.error : UOS_ERROR;
    }
    return 0;
}

int64_t udp_send_until(uint64_t socket,const void *data,size_t bytes,int64_t deadline)
{
    for (;;) {
        int64_t now=uos_time_ms();
        if (now<0) { return now; }
        if (now>=deadline) { return UOS_ETIMEDOUT; }
        int64_t result=uos_udp_send(socket,data,bytes);
        if (result!=UOS_EAGAIN) { return result; }
        result=udp_flush_until(socket,deadline);
        if (result) { return result; }
    }
}

int64_t udp_recv_until(uint64_t socket,void *data,size_t capacity,int64_t deadline)
{
    for (;;) {
        int64_t ready=wait_ready(socket,UOS_EVENT_READ,deadline);
        if (ready<0) { return ready; }
        if (ready&UOS_EVENT_HANGUP) { return UOS_ENETDOWN; }
        int64_t result=uos_udp_recv(socket,data,capacity,0);
        if (result!=UOS_EAGAIN) { return result; }
    }
}
