/* Local IPv4 datagrams and directed neighbor resolution. RFC 768, RFC 826. */
#include "udp.h"
#include "net_wire.h"
#include <string.h>

enum { UDP_SEND_MS=3000,UDP_ARP_MS=1000,UDP_NEIGHBOR_MS=30000 };
static struct udp_socket {
    uint64_t owner,handle,deadline,next_arp,learned;
    struct uos_udp_peer peer;
    unsigned char neighbor[6];
    bool sending,received,known;
    unsigned arp_sent;
    int64_t send_error;
    size_t tx_bytes,rx_bytes;
    unsigned char tx[UOS_UDP_MAX_BYTES],rx[UOS_UDP_MAX_BYTES];
} sockets[UDP_SOCKET_LIMIT];

static struct {
    bool online;
    unsigned char mac[6],ip[4];
    uint32_t mask;
    unsigned next;
} link;
static uint64_t next_handle;
static uint16_t next_port=49152;

static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static uint32_t address(const unsigned char p[4]) { return (uint32_t)net_be16(p)<<16|net_be16(p+2); }
static bool unicast_ip(const unsigned char *ip) { return ip[0] && ip[0]!=127 && ip[0]<224; }
static bool unicast_mac(const unsigned char *mac)
{
    static const unsigned char empty[6]={0};
    return mac && !(mac[0]&1) && !net_equal(mac,empty,6);
}
static bool on_link(const unsigned char *ip)
{
    uint32_t value=address(ip),subnet=address(link.ip)&link.mask,host=value&~link.mask;
    return unicast_ip(ip) && (value&link.mask)==subnet && host && host!=~link.mask;
}
static bool neighbor_valid(const struct udp_socket *s,uint64_t now)
{ return s->known && now-s->learned<UDP_NEIGHBOR_MS; }

bool udp_encode(const struct udp_packet *packet,void *output,size_t capacity,size_t *actual)
{
    if (!actual) { return false; }*actual=0;
    if (!packet || !output || !packet->local_ip || !packet->peer_ip || !unicast_mac(packet->local_mac) ||
        !unicast_ip(packet->local_ip) || !unicast_ip(packet->peer_ip) || net_equal(packet->local_ip,packet->peer_ip,4) ||
        packet->bytes>UOS_UDP_MAX_BYTES || (packet->bytes && !packet->payload)) { return false; }
    bool arp=!packet->peer_mac;
    if (!arp && (!unicast_mac(packet->peer_mac) || !packet->local_port || !packet->peer_port)) { return false; }
    size_t bytes=arp || 42+packet->bytes<60 ? 60 : 42+packet->bytes;
    if (capacity<bytes) { return false; }
    unsigned char *p=output;
    memset(p,0,bytes);
    if (arp) { memset(p,255,6); }else { memcpy(p,packet->peer_mac,6); }
    memcpy(p+6,packet->local_mac,6);
    if (arp) {
        put16(p+12,0x0806);
        unsigned char *a=p+14;
        put16(a,1);put16(a+2,0x0800);a[4]=6;a[5]=4;put16(a+6,1);
        memcpy(a+8,packet->local_mac,6);memcpy(a+14,packet->local_ip,4);memcpy(a+24,packet->peer_ip,4);
    } else {
        put16(p+12,0x0800);
        unsigned char *ip=p+14,*u=p+34;
        ip[0]=0x45;put16(ip+2,28+(unsigned)packet->bytes);put16(ip+6,0x4000);ip[8]=64;ip[9]=17;
        memcpy(ip+12,packet->local_ip,4);memcpy(ip+16,packet->peer_ip,4);
        put16(u,packet->local_port);put16(u+2,packet->peer_port);put16(u+4,8+(unsigned)packet->bytes);
        if (packet->bytes) { memcpy(u+8,packet->payload,packet->bytes); }
        unsigned sum=net_udp_checksum(ip,u,8+packet->bytes);
        put16(u+6,sum ? sum : 65535);put16(ip+10,net_checksum(ip,20,0));
    }
    *actual=bytes;return true;
}

bool udp_link_up(const unsigned char mac[6],const unsigned char ip[4],const unsigned char mask[4])
{
    if (link.online || !unicast_mac(mac) || !ip || !mask) { return false; }
    uint32_t bits=address(mask),inverse=~bits;
    if (!bits || !inverse || (inverse&(inverse+1))) { return false; }
    memcpy(link.mac,mac,6);memcpy(link.ip,ip,4);link.mask=bits;
    link.online=on_link(ip);return link.online;
}
void udp_link_down(void)
{ memset(sockets,0,sizeof(sockets));memset(&link,0,sizeof(link)); }

static struct udp_socket *find(uint64_t owner,uint64_t handle)
{
    if (!owner || !handle) { return NULL; }
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        if (sockets[i].owner==owner && sockets[i].handle==handle) { return &sockets[i]; }
    }
    return NULL;
}
static bool port_used(uint16_t port)
{
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        if (sockets[i].owner && sockets[i].peer.local_port==port) { return true; }
    }
    return false;
}
int64_t udp_open(uint64_t owner,const struct uos_udp_peer *peer)
{
    if (!owner || !peer || !peer->port || (peer->local_port && peer->local_port<1024)) { return UOS_ERROR; }
    if (!link.online) { return UOS_ENETDOWN; }
    if (!on_link(peer->ipv4) || net_equal(peer->ipv4,link.ip,4)) { return UOS_EHOSTUNREACH; }
    struct udp_socket *slot=NULL;unsigned owned=0;
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        if (sockets[i].owner==owner) { ++owned; }
        if (!sockets[i].owner && !slot) { slot=&sockets[i]; }
    }
    if (!slot || owned>=UDP_OWNER_LIMIT || next_handle==INT64_MAX) { return UOS_ENOSPC; }
    uint16_t local_port=peer->local_port;
    if (local_port && port_used(local_port)) { return UOS_EBUSY; }
    if (!local_port) {
        do {
            local_port=next_port;
            next_port=next_port==65535 ? 49152 : next_port+1;
        } while (port_used(local_port));
    }
    slot->owner=owner;slot->handle=++next_handle;slot->peer=*peer;slot->peer.local_port=local_port;
    return (int64_t)slot->handle;
}
int64_t udp_send(uint64_t owner,uint64_t handle,const void *payload,size_t bytes,uint64_t now)
{
    if (!link.online) { return UOS_ENETDOWN; }
    struct udp_socket *s=find(owner,handle);
    if (!s) { return UOS_EBADF; }
    if (bytes>UOS_UDP_MAX_BYTES || (bytes && !payload)) { return UOS_EMSGSIZE; }
    if (s->sending) { return UOS_EAGAIN; }
    if (s->send_error) { int64_t error=s->send_error;s->send_error=0;return error; }
    if (bytes) { memcpy(s->tx,payload,bytes); }
    s->tx_bytes=bytes;s->sending=true;s->deadline=now+UDP_SEND_MS;s->next_arp=now;s->arp_sent=0;
    return (int64_t)bytes;
}
int64_t udp_recv(uint64_t owner,uint64_t handle,void *buffer,size_t capacity)
{
    if (!link.online) { return UOS_ENETDOWN; }
    struct udp_socket *s=find(owner,handle);
    if (!s) { return UOS_EBADF; }
    if (s->received) {
        if (capacity<s->rx_bytes || (s->rx_bytes && !buffer)) { return UOS_EMSGSIZE; }
        size_t bytes=s->rx_bytes;
        if (bytes) { memcpy(buffer,s->rx,bytes);memset(s->rx,0,bytes); }
        s->received=false;s->rx_bytes=0;return (int64_t)bytes;
    }
    int64_t error=s->send_error;s->send_error=0;return error ? error : UOS_EAGAIN;
}
int64_t udp_close(uint64_t owner,uint64_t handle)
{
    struct udp_socket *s=find(owner,handle);
    if (!s) { return UOS_EBADF; }
    memset(s,0,sizeof(*s));return 0;
}
unsigned udp_ready(uint64_t owner,uint64_t handle)
{
    const struct udp_socket *s=find(owner,handle);
    if (!link.online || !s) { return UOS_EVENT_HANGUP; }
    unsigned ready=s->received ? UOS_EVENT_READ : 0;
    if (s->send_error) { return ready|UOS_EVENT_ERROR; }
    return !s->sending ? ready|UOS_EVENT_WRITE : ready;
}
void udp_close_owner(uint64_t owner)
{
    if (!owner) { return; }
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        if (sockets[i].owner==owner) { memset(&sockets[i],0,sizeof(sockets[i])); }
    }
}
unsigned udp_open_count(void)
{
    unsigned count=0;
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) { count+=sockets[i].owner!=0; }
    return count;
}

static void receive_arp(const unsigned char *p,uint64_t now)
{
    const unsigned char *a=p+14;
    if (net_be16(a)!=1 || net_be16(a+2)!=0x0800 || a[4]!=6 || a[5]!=4 || net_be16(a+6)!=2 ||
        !net_equal(a+8,p+6,6) || !net_equal(a+18,link.mac,6) || !net_equal(a+24,link.ip,4)) { return; }
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        struct udp_socket *s=&sockets[i];
        if (s->sending && s->arp_sent && (int64_t)(now-s->deadline)<0 && !neighbor_valid(s,now) && net_equal(a+14,s->peer.ipv4,4)) {
            memcpy(s->neighbor,a+8,6);s->learned=now;s->known=true;
        }
    }
}
void udp_receive(const void *frame,size_t bytes,uint64_t now)
{
    const unsigned char *p=frame;
    if (!link.online || !p || bytes<42 || bytes>1514 || !net_equal(p,link.mac,6) || !unicast_mac(p+6) || net_equal(p+6,link.mac,6)) { return; }
    unsigned type=net_be16(p+12);
    if (type==0x0806) { receive_arp(p,now);return; }
    const unsigned char *ip=p+14,*u=p+34;unsigned total=net_be16(ip+2);
    if (type!=0x0800 || ip[0]!=0x45 || total<28 || total>28+UOS_UDP_MAX_BYTES || total>bytes-14 ||
        (net_be16(ip+6)&~0x4000u) || !ip[8] || ip[9]!=17 || !net_equal(ip+16,link.ip,4) || net_checksum(ip,20,0) ||
        net_be16(u+4)!=total-20 || (net_be16(u+6) && net_udp_checksum(ip,u,total-20))) { return; }
    unsigned source=net_be16(u),dest=net_be16(u+2);
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        struct udp_socket *s=&sockets[i];
        if (!s->owner || s->peer.local_port!=dest || s->peer.port!=source || !net_equal(s->peer.ipv4,ip+12,4)) { continue; }
        if (!s->received && (!neighbor_valid(s,now) || net_equal(s->neighbor,p+6,6))) {
            s->rx_bytes=total-28;memcpy(s->rx,u+8,s->rx_bytes);s->received=true;
        }
        return;
    }
}
static void sent(struct udp_socket *s,int64_t error)
{
    memset(s->tx,0,s->tx_bytes);s->tx_bytes=0;s->sending=false;s->send_error=error;
}
enum block_result udp_service(udp_transmit transmit,void *context,uint64_t now)
{
    if (!link.online || !transmit) { return BLOCK_NOT_READY; }
    for (unsigned i=0;i<UDP_SOCKET_LIMIT;++i) {
        unsigned index=(link.next+i)%UDP_SOCKET_LIMIT;
        struct udp_socket *s=&sockets[index];
        if (!s->sending) { continue; }
        bool known=neighbor_valid(s,now);
        if ((int64_t)(now-s->deadline)>=0) { sent(s,known ? UOS_ETIMEDOUT : UOS_EHOSTUNREACH);continue; }
        if (!known && (s->arp_sent>=3 || (int64_t)(now-s->next_arp)<0)) { continue; }
        const struct udp_packet packet={link.mac,link.ip,known ? s->neighbor : NULL,s->peer.ipv4,
            s->peer.local_port,s->peer.port,s->tx,s->tx_bytes};
        link.next=(index+1)%UDP_SOCKET_LIMIT;
        enum block_result r=transmit(context,&packet);
        if (r==BLOCK_NOT_READY) { return BLOCK_OK; }
        if (r!=BLOCK_OK) { return r; }
        if (known) { sent(s,0); }else { ++s->arp_sent;s->next_arp=now+UDP_ARP_MS; }
        return BLOCK_OK;
    }
    return BLOCK_OK;
}
