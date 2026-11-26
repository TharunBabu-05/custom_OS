#include "tcp.h"
#include "net_wire.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static const unsigned char local_mac[]={2,0,0,0,0,1},remote_mac[]={2,0,0,0,0,2};
static const unsigned char local_ip[]={192,0,2,1},remote_ip[]={192,0,2,2},mask[]={255,255,255,0},secret[32]={7};
static const struct uos_tcp_peer ep={{192,0,2,2},4041,0};
static unsigned sends;
static struct tcp_packet last;
static unsigned char sent[TCP_FRAME_MAX];
static uint16_t port;

static enum block_result tx(void *ctx,const struct tcp_packet *p)
{
    (void)ctx;size_t bytes;
    assert(tcp_packet_pending(p) && tcp_encode(p,sent,sizeof(sent),&bytes));last=*p;++sends;port=p->local_port;
    if (p->peer_mac) {
        assert(!net_checksum(sent+14,20,0) && !net_transport_checksum(sent+14,sent+34,net_be16(sent+16)-20,6));
    }
    return BLOCK_OK;
}
static void tick(uint64_t now) { assert(tcp_service(tx,NULL,now)==BLOCK_OK); }
static void arp(void)
{
    unsigned char p[60]={0};const unsigned char header[]={8,6,0,1,8,0,6,4,0,2};
    memcpy(p,local_mac,6);memcpy(p+6,remote_mac,6);memcpy(p+12,header,sizeof(header));
    memcpy(p+22,remote_mac,6);memcpy(p+28,remote_ip,4);memcpy(p+32,local_mac,6);memcpy(p+38,local_ip,4);
    tcp_receive(p,sizeof(p));
}
static void segment(unsigned flags,uint32_t seq,uint32_t ack,unsigned window,const char *data,size_t n,bool corrupt)
{
    unsigned char p[TCP_FRAME_MAX];size_t bytes;
    const struct tcp_packet packet={remote_mac,remote_ip,local_mac,local_ip,seq,ack,4041,port,(uint16_t)window,flags,data,n};
    assert(tcp_encode(&packet,p,sizeof(p),&bytes));
    if (corrupt) { p[50]^=1; }
    tcp_receive(p,bytes);
}
int main(void)
{
    assert(tcp_open(1,&ep,0)==UOS_ENETDOWN && tcp_link_up(local_mac,local_ip,mask,secret));
    struct uos_tcp_peer offsite=ep;offsite.ipv4[2]=3;
    assert(tcp_open(1,&offsite,0)==UOS_EHOSTUNREACH);
    int64_t h=tcp_open(1,&ep,0);assert(h>0 && tcp_status(1,h)==UOS_EAGAIN);
    assert(!tcp_ready(1,h) && tcp_ready(2,h)==UOS_EVENT_HANGUP);
    assert(tcp_status(2,h)==UOS_EBADF && tcp_close(2,h,0)==UOS_EBADF && tcp_read(2,h,NULL,0)==UOS_EBADF);
    arp();tick(0);assert(sends==1 && !last.peer_mac);arp();tick(1);
    assert(sends==2 && last.flags==TCP_SYN && sent[46]==0x60 && net_be16(sent+56)==512);
    uint32_t isn=last.seq;
    tick(999);assert(sends==2);tick(1001);assert(sends==3 && last.seq==isn && last.flags==TCP_SYN);
    uint32_t seq=UINT32_MAX-1,ack=isn+1;
    segment(TCP_SYN|TCP_ACK,seq,ack,20,NULL,0,true);assert(tcp_status(1,h)==UOS_EAGAIN);
    segment(TCP_SYN|TCP_ACK,seq,ack+1,20,NULL,0,false);assert(tcp_status(1,h)==UOS_EAGAIN);
    segment(TCP_SYN|TCP_ACK,seq,ack,20,NULL,0,false);assert(tcp_status(1,h)==0);++seq;
    assert(tcp_ready(1,h)==UOS_EVENT_WRITE);
    tick(1002);assert(sends==4 && last.flags==TCP_ACK && last.seq==ack && last.ack==seq);

    assert(tcp_write(1,h,"abcde",5)==5 && tcp_write(1,h,"x",1)==UOS_EAGAIN);
    assert(!tcp_ready(1,h));
    tick(1003);assert(sends==5 && last.flags==(TCP_ACK|TCP_PSH) && last.seq==ack && last.bytes==5 && !memcmp(sent+54,"abcde",5));
    tick(2003);assert(sends==6 && last.seq==ack && !memcmp(sent+54,"abcde",5));
    segment(TCP_ACK,seq,ack+2,2,NULL,0,false);ack+=2;
    tick(2004);assert(last.seq==ack && last.bytes==2 && !memcmp(sent+54,"cd",2));
    segment(TCP_ACK,seq,ack+3,20,NULL,0,false);ack+=3;
    assert(tcp_write(1,h,NULL,0)==0);

    char read[8];
    segment(TCP_ACK|TCP_PSH,seq+2,ack,20,"cd",2,false);
    assert(tcp_read(1,h,read,sizeof(read))==UOS_EAGAIN);tick(2005);assert(last.ack==seq);
    segment(TCP_ACK|TCP_PSH,seq,ack,20,"abc",3,false);
    segment(TCP_ACK|TCP_PSH,seq+1,ack,20,"bcde",4,false);
    segment(TCP_ACK|TCP_PSH,seq,ack,20,"abc",3,false);seq+=5;
    assert(tcp_ready(1,h)==(UOS_EVENT_READ|UOS_EVENT_WRITE));
    assert(tcp_read(1,h,read,2)==2 && !memcmp(read,"ab",2));
    assert(tcp_read(1,h,read,sizeof(read))==3 && !memcmp(read,"cde",3));
    segment(TCP_RST,seq+1,0,0,NULL,0,false);assert(tcp_status(1,h)==0);
    segment(TCP_ACK|TCP_FIN,seq,ack,20,NULL,0,false);++seq;
    assert(tcp_ready(1,h)&UOS_EVENT_READ);
    assert(tcp_read(1,h,read,sizeof(read))==0 && (tcp_ready(1,h)&UOS_EVENT_READ));
    assert(tcp_close(1,h,2006)==0 && tcp_status(1,h)==UOS_EBADF);
    assert(tcp_ready(1,h)==UOS_EVENT_HANGUP);
    tick(2006);assert(last.flags==(TCP_FIN|TCP_ACK) && last.ack==seq && last.seq==ack);
    segment(TCP_ACK,seq,ack+1,20,NULL,0,false);tick(2007);
    segment(TCP_ACK|TCP_FIN,seq-1,ack+1,20,NULL,0,false);tick(2008);
    assert(last.flags==TCP_ACK && last.ack==seq && last.seq==ack+1);

    h=tcp_open(1,&ep,3000);assert(h>0);tick(3000);arp();tick(3001);isn=last.seq;
    segment(TCP_RST|TCP_ACK,0,isn,0,NULL,0,false);assert(tcp_status(1,h)==UOS_EAGAIN);
    segment(TCP_RST|TCP_ACK,0,isn+1,0,NULL,0,false);
    assert(tcp_ready(1,h)==UOS_EVENT_ERROR && tcp_ready(1,h)==UOS_EVENT_ERROR);
    assert(tcp_status(1,h)==UOS_ECONNRESET && tcp_close(1,h,3001)==0);
    h=tcp_open(1,&ep,4000);assert(h>0);tick(4000);tick(5000);tick(6000);tick(7000);
    assert(tcp_status(1,h)==UOS_EHOSTUNREACH && tcp_close(1,h,7000)==0);
    h=tcp_open(1,&ep,8000);assert(h>0);tick(8000);arp();tick(8001);tick(9001);tick(11001);tick(15001);tick(23001);
    assert(tcp_status(1,h)==UOS_ETIMEDOUT);tcp_close_owner(1,23001);assert(!tcp_open_count());
    h=tcp_open(1,&ep,24000);assert(h>0);tcp_link_down();assert(!tcp_open_count() && tcp_status(1,h)==UOS_ENETDOWN);
    assert(tcp_ready(1,h)==UOS_EVENT_HANGUP);
    assert(tcp_link_up(local_mac,local_ip,mask,secret) && tcp_status(1,h)==UOS_EBADF);tcp_link_down();
    puts("PASS: TCP handshake, lost SYN/data, partial ACK, reordering, duplicate/overlap, sequence wrap, FIN, reset, timeout and ownership.");return 0;
}
