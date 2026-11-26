#include "udp.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static const unsigned char local_mac[]={2,0,0,0,0,1},peer_mac[]={2,0,0,0,0,2};
static const unsigned char local_ip[]={192,0,2,1},mask[]={255,255,255,0};
static const struct uos_udp_peer target={{192,0,2,2},4040,55001};
static struct {
    enum block_result result;
    unsigned calls,arp,udp;
    size_t bytes;
    unsigned char frame[UDP_FRAME_MAX];
} wire;

static enum block_result transmit(void *ctx,const struct udp_packet *packet)
{
    assert(ctx==&wire && udp_encode(packet,wire.frame,sizeof(wire.frame),&wire.bytes));
    ++wire.calls;
    if (packet->peer_mac) { ++wire.udp; }else { ++wire.arp; }
    return wire.result;
}
static void arp(unsigned char frame[60])
{
    const unsigned char header[]={0x08,0x06,0,1,0x08,0,6,4,0,2};
    memset(frame,0,60);memcpy(frame,local_mac,6);memcpy(frame+6,peer_mac,6);memcpy(frame+12,header,sizeof(header));
    memcpy(frame+22,peer_mac,6);memcpy(frame+28,target.ipv4,4);memcpy(frame+32,local_mac,6);memcpy(frame+38,local_ip,4);
}
static size_t reply(unsigned char *frame,unsigned port,const void *payload,size_t bytes)
{
    const struct udp_packet packet={peer_mac,target.ipv4,local_mac,local_ip,(uint16_t)port,target.local_port,payload,bytes};
    size_t n=0;assert(udp_encode(&packet,frame,UDP_FRAME_MAX,&n));return n;
}
static void step(uint64_t now)
{ assert(udp_service(transmit,&wire,now)==BLOCK_OK); }

int main(void)
{
    assert(udp_open(1,&target)==UOS_ENETDOWN);
    assert(udp_link_up(local_mac,local_ip,mask) && !udp_link_up(local_mac,local_ip,mask));
    struct uos_udp_peer bad=target;bad.ipv4[3]=255;
    assert(udp_open(1,&bad)==UOS_EHOSTUNREACH);bad.ipv4[3]=0;
    assert(udp_open(1,&bad)==UOS_EHOSTUNREACH);bad.ipv4[3]=1;
    assert(udp_open(1,&bad)==UOS_EHOSTUNREACH);bad.ipv4[2]=3;
    assert(udp_open(1,&bad)==UOS_EHOSTUNREACH);bad=target;bad.port=0;
    assert(udp_open(1,&bad)==UOS_ERROR);bad=target;bad.local_port=68;
    assert(udp_open(1,&bad)==UOS_ERROR);

    int64_t handle=udp_open(1,&target);assert(handle>0);
    assert(udp_ready(1,handle)==UOS_EVENT_WRITE && udp_ready(2,handle)==UOS_EVENT_HANGUP);
    assert(udp_open(2,&target)==UOS_EBUSY);
    struct uos_udp_peer automatic=target;automatic.local_port=0;
    assert(udp_open(1,&automatic)>0 && udp_open(1,&automatic)==UOS_ENOSPC);
    assert(udp_open(2,&automatic)>0 && udp_open(3,&automatic)>0 && udp_open(3,&automatic)==UOS_ENOSPC);
    assert(udp_open_count()==4);
    assert(udp_recv(2,handle,NULL,0)==UOS_EBADF && udp_send(2,handle,NULL,0,0)==UOS_EBADF && udp_close(2,handle)==UOS_EBADF);
    udp_close_owner(2);udp_close_owner(3);assert(udp_open_count()==2);

    unsigned char buffer[UOS_UDP_MAX_BYTES],frame[UDP_FRAME_MAX];
    const unsigned char message[]={0xa0,0,0x7f};
    assert(udp_send(1,handle,message,sizeof(message),0)==sizeof(message));
    assert(!udp_ready(1,handle));
    assert(udp_send(1,handle,NULL,0,0)==UOS_EAGAIN);
    arp(frame);udp_receive(frame,60,0); /* An unsolicited response cannot prime the cache. */
    wire.result=BLOCK_NOT_READY;step(0);udp_receive(frame,60,1);
    wire.result=BLOCK_OK;step(1);assert(wire.calls==2 && wire.arp==2 && !wire.udp && wire.bytes==60);
    const unsigned char expected_arp[]={255,255,255,255,255,255,2,0,0,0,0,1,8,6,0,1,8,0,6,4,0,1,
        2,0,0,0,0,1,192,0,2,1,0,0,0,0,0,0,192,0,2,2};
    assert(!memcmp(wire.frame,expected_arp,sizeof(expected_arp)));
    frame[27]^=1;udp_receive(frame,60,2);step(2);assert(wire.calls==2);
    frame[27]^=1;udp_receive(frame,60,3);step(3);assert(wire.udp==1 && wire.bytes==60);
    const unsigned char expected_ip[]={0x45,0,0,31,0,0,0x40,0,64,17,0xb6,0xca,192,0,2,1,192,0,2,2};
    assert(!memcmp(wire.frame,peer_mac,6) && !memcmp(wire.frame+14,expected_ip,sizeof(expected_ip)));
    assert(!memcmp(wire.frame+42,message,sizeof(message)));
    for (unsigned i=45;i<60;++i) { assert(!wire.frame[i]); }

    size_t n=reply(frame,target.port+1,message,sizeof(message));udp_receive(frame,n,4);
    n=reply(frame,target.port,message,sizeof(message));
    for (unsigned bytes=0;bytes<45;++bytes) { udp_receive(frame,bytes,4); }
    frame[43]^=1;udp_receive(frame,n,4);frame[43]^=1;
    frame[11]^=1;udp_receive(frame,n,4);frame[11]^=1;
    assert(udp_recv(1,handle,buffer,sizeof(buffer))==UOS_EAGAIN);
    udp_receive(frame,n,4);
    assert(udp_ready(1,handle)==(UOS_EVENT_READ|UOS_EVENT_WRITE));
    assert(udp_ready(1,handle)&UOS_EVENT_READ);
    n=reply(frame,target.port,"later",5);udp_receive(frame,n,4); /* Full queue retains its first packet. */
    assert(udp_recv(1,handle,buffer,2)==UOS_EMSGSIZE);
    assert(udp_recv(1,handle,buffer,sizeof(buffer))==sizeof(message) && !memcmp(buffer,message,sizeof(message)));
    assert(udp_ready(1,handle)==UOS_EVENT_WRITE);
    assert(udp_recv(1,handle,buffer,sizeof(buffer))==UOS_EAGAIN);
    n=reply(frame,target.port,NULL,0);udp_receive(frame,n,5);
    struct uos_socket_info info={.version=UINT32_MAX};
    assert(udp_socket_info(2,handle,&info)==UOS_EBADF && info.version==UINT32_MAX);
    assert(!udp_socket_info(1,handle,&info) && info.state==UOS_SOCKET_DATAGRAM && !info.rx_bytes &&
           (info.flags&UOS_SOCKET_RX_PENDING) && info.local_port==target.local_port && info.peer_port==target.port);
    assert((udp_ready(1,handle)&UOS_EVENT_READ) && udp_recv(1,handle,NULL,0)==0);
    n=reply(frame,target.port,message,sizeof(message));frame[40]=frame[41]=0;udp_receive(frame,n,5);
    assert(udp_recv(1,handle,buffer,sizeof(buffer))==sizeof(message));

    const uint64_t expired=31000;
    assert(udp_send(1,handle,NULL,0,expired)==0);
    step(expired);step(expired+999);step(expired+1000);step(expired+2000);step(expired+3000);
    assert(!udp_socket_info(1,handle,&info) && info.error==UOS_EHOSTUNREACH && info.ready==UOS_EVENT_ERROR);
    assert(udp_ready(1,handle)==UOS_EVENT_ERROR && udp_ready(1,handle)==UOS_EVENT_ERROR);
    assert(wire.arp==5 && wire.udp==1 && udp_recv(1,handle,NULL,0)==UOS_EHOSTUNREACH);
    assert(udp_ready(1,handle)==UOS_EVENT_WRITE);
    assert(udp_recv(1,handle,NULL,0)==UOS_EAGAIN);
    assert(udp_send(1,handle,NULL,0,expired+3000)==0);step(expired+3000);
    arp(frame);udp_receive(frame,60,expired+3001);step(expired+3001);
    assert(wire.udp==2);
    struct uos_udp_stats stats;
    udp_stats(&stats);
    assert(stats.session.online && stats.session.used==2 && stats.session.limit==4 && stats.session.prefix==24 &&
           !memcmp(stats.session.ipv4,local_ip,4) && stats.arp_requests==5 && stats.arp_failures==1 &&
           stats.datagrams_tx==2 && stats.datagrams_rx==3 && stats.bytes_tx==3 && stats.bytes_rx==6 && stats.full_drops==1);
    uint64_t session=stats.session.generation;

    assert(udp_send(1,handle,message,sizeof(message),expired+3002)==sizeof(message));
    udp_link_down();assert(!udp_open_count() && udp_recv(1,handle,NULL,0)==UOS_ENETDOWN && udp_service(transmit,&wire,0)==BLOCK_NOT_READY);
    assert(udp_ready(1,handle)==UOS_EVENT_HANGUP);
    udp_stats(&stats);
    assert(!stats.session.online && !stats.session.used && !stats.session.prefix && stats.full_drops==1);
    assert(udp_link_up(local_mac,local_ip,mask));
    udp_stats(&stats);
    assert(stats.session.generation==session+1 && !stats.full_drops && !stats.datagrams_tx);
    int64_t next=udp_open(1,&target);assert(next>handle && udp_close(1,handle)==UOS_EBADF && udp_close(1,next)==0);
    assert(!udp_open_count());udp_link_down();
    puts("PASS: UDP packet bytes, checksum and peer filtering, queue and handle bounds, ARP timeout/recovery and link cleanup.");
    return 0;
}
