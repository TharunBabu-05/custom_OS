/* Bounded active-open TCP for a directly reachable IPv4 peer. RFC 9293. */
#include "tcp.h"
#include "udp.h"
#include "net_wire.h"
#include "crypto.h"
#include <string.h>

enum { SLOT_COUNT=3, RX_BYTES=1024, CLOSE_MS=20000, TIME_WAIT_MS=60000 };
enum tcp_phase { FREE, ARP, SYN, OPEN, TIME_WAIT, FAILED };
static struct connection {
    uint64_t owner,handle,due,close_by;
    enum tcp_phase phase;
    int64_t error;
    struct uos_tcp_peer peer;
    unsigned char mac[6];
    uint16_t port,window,mss;
    uint32_t una,next,expect,window_seq,window_ack;
    unsigned tries,tx_bytes,inflight,rx_head,rx_bytes;
    bool ack,peer_fin,fin_sent,fin_acked;
    unsigned char tx[UOS_TCP_MAX_BYTES],rx[RX_BYTES];
} sockets[SLOT_COUNT];
static struct {
    bool online;
    unsigned char mac[6],ip[4],secret[32];
    uint32_t mask;
    unsigned next;
} link;
static uint64_t generation;
static uint16_t local_port=49152;

static bool mac_valid(const unsigned char *p)
{
    static const unsigned char empty[6]={0};
    return p && !(p[0]&1) && !net_equal(p,empty,6);
}
static bool ip_valid(const unsigned char *p) { return p && p[0] && p[0]!=127 && p[0]<224; }
static bool local_peer(const unsigned char *ip)
{
    uint32_t v=net_be32(ip),host=v&~link.mask;
    return ip_valid(ip) && host && host!=~link.mask && (v&link.mask)==(net_be32(link.ip)&link.mask);
}
static bool before(uint32_t a,uint32_t b) { return (int32_t)(a-b)<0; }
static unsigned min(unsigned a,unsigned b) { return a<b ? a : b; }

bool tcp_encode(const struct tcp_packet *s,void *out,size_t capacity,size_t *actual)
{
    if (!actual) { return false; }*actual=0;
    if (!s || !out || !mac_valid(s->local_mac) || !ip_valid(s->local_ip) || !ip_valid(s->peer_ip) ||
        net_equal(s->local_ip,s->peer_ip,4) || s->bytes>UOS_TCP_MAX_BYTES || (s->bytes && !s->payload)) { return false; }
    if (!s->peer_mac) {
        const struct udp_packet arp={.local_mac=s->local_mac,.local_ip=s->local_ip,.peer_ip=s->peer_ip};
        return udp_encode(&arp,out,capacity,actual);
    }
    if (!mac_valid(s->peer_mac) || !s->local_port || !s->peer_port || !s->flags || (s->flags&~31u) ||
        ((s->flags&(TCP_SYN|TCP_RST)) && s->bytes)) { return false; }
    unsigned header=s->flags&TCP_SYN ? 24 : 20;
    unsigned length=header+(unsigned)s->bytes,frame=34+length;
    if (frame<60) { frame=60; }if (capacity<frame) { return false; }
    unsigned char *p=out,*v=p+14,*t=p+34;
    memset(p,0,frame);memcpy(p,s->peer_mac,6);memcpy(p+6,s->local_mac,6);net_put16(p+12,0x800);
    v[0]=0x45;net_put16(v+2,20+length);net_put16(v+6,0x4000);v[8]=64;v[9]=6;
    memcpy(v+12,s->local_ip,4);memcpy(v+16,s->peer_ip,4);net_put16(v+10,net_checksum(v,20,0));
    net_put16(t,s->local_port);net_put16(t+2,s->peer_port);net_put32(t+4,s->seq);net_put32(t+8,s->ack);
    t[12]=(unsigned char)(header<<2);t[13]=(unsigned char)s->flags;net_put16(t+14,s->window);
    if (header==24) { t[20]=2;t[21]=4;net_put16(t+22,UOS_TCP_MAX_BYTES); }
    if (s->bytes) { memcpy(t+header,s->payload,s->bytes); }
    net_put16(t+16,net_transport_checksum(v,t,length,6));*actual=frame;return true;
}

bool tcp_link_up(const unsigned char mac[6],const unsigned char ip[4],const unsigned char mask[4],const unsigned char secret[32])
{
    if (link.online || !mac_valid(mac) || !ip_valid(ip) || !mask || !secret) { return false; }
    uint32_t m=net_be32(mask),inverse=~m;
    if (!m || !inverse || (inverse&(inverse+1))) { return false; }
    memcpy(link.mac,mac,6);memcpy(link.ip,ip,4);memcpy(link.secret,secret,32);link.mask=m;
    link.online=local_peer(ip);return link.online;
}
bool tcp_packet_pending(const struct tcp_packet *p)
{
    if (!p || !link.online) { return false; }
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        const struct connection *s=&sockets[i];
        if (p->peer_ip!=s->peer.ipv4 || p->local_port!=s->port) { continue; }
        if (!p->peer_mac) { return s->phase==ARP; }
        if (p->flags==TCP_SYN) { return s->phase==SYN; }
        if ((s->phase!=OPEN && s->phase!=TIME_WAIT) || p->ack!=s->expect) { return false; }
        return p->bytes || (p->flags&TCP_FIN) ? p->seq==s->una : p->seq==s->next;
    }
    return false;
}
void tcp_link_down(void)
{ memset(sockets,0,sizeof(sockets));crypto_wipe(&link,sizeof(link)); }

static struct connection *lookup(uint64_t owner,uint64_t handle)
{
    if (!owner || !handle) { return NULL; }
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        if (sockets[i].owner==owner && sockets[i].handle==handle) { return &sockets[i]; }
    }
    return NULL;
}
static bool port_used(uint16_t port)
{
    for (unsigned i=0;i<SLOT_COUNT;++i) { if (sockets[i].phase!=FREE && sockets[i].port==port) { return true; } }
    return false;
}
int64_t tcp_open(uint64_t owner,const struct uos_tcp_peer *peer,uint64_t now)
{
    if (!owner || !peer || !peer->port || peer->reserved) { return UOS_ERROR; }
    if (!link.online) { return UOS_ENETDOWN; }
    if (!local_peer(peer->ipv4) || net_equal(peer->ipv4,link.ip,4)) { return UOS_EHOSTUNREACH; }
    struct connection *slot=NULL;unsigned owned=0;
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        if (sockets[i].owner==owner) { ++owned; }
        if (!slot && sockets[i].phase==FREE) { slot=&sockets[i]; }
    }
    if (!slot || owned==2 || generation==INT64_MAX) { return UOS_ENOSPC; }
    do { local_port=local_port==65535 ? 49152 : local_port+1; } while (port_used(local_port));
    uint64_t handle=++generation;
    unsigned char hash[32];
    const struct crypto_span input[]={{&handle,sizeof(handle)}};
    if (!hmac_sha256(link.secret,sizeof(link.secret),input,1,hash)) { return UOS_ERROR; }
    slot->una=slot->next=net_be32(hash);crypto_wipe(hash,sizeof(hash));
    slot->phase=ARP;slot->owner=owner;slot->handle=handle;slot->port=local_port;
    slot->peer=*peer;slot->mss=UOS_TCP_MAX_BYTES;slot->due=now;return (int64_t)handle;
}
static int64_t result(const struct connection *s)
{ return s->phase==FAILED ? s->error : s->phase==OPEN ? 0 : UOS_EAGAIN; }
int64_t tcp_status(uint64_t owner,uint64_t handle)
{
    if (!link.online) { return UOS_ENETDOWN; }
    const struct connection *s=lookup(owner,handle);return s ? result(s) : UOS_EBADF;
}
int64_t tcp_write(uint64_t owner,uint64_t handle,const void *data,size_t n)
{
    if (!link.online) { return UOS_ENETDOWN; }
    struct connection *s=lookup(owner,handle);
    if (!s) { return UOS_EBADF; }if (result(s)) { return result(s); }
    if (n>UOS_TCP_MAX_BYTES || (n && !data)) { return UOS_EMSGSIZE; }
    if (!n) { return 0; }if (s->tx_bytes) { return UOS_EAGAIN; }
    memcpy(s->tx,data,n);s->tx_bytes=(unsigned)n;return (int64_t)n;
}
int64_t tcp_read(uint64_t owner,uint64_t handle,void *data,size_t n)
{
    if (!link.online) { return UOS_ENETDOWN; }
    struct connection *s=lookup(owner,handle);
    if (!s) { return UOS_EBADF; }if (result(s)) { return result(s); }
    if (n>UOS_TCP_MAX_BYTES || (n && !data)) { return UOS_EMSGSIZE; }
    if (!n) { return 0; }
    unsigned count=min((unsigned)n,s->rx_bytes);
    if (!count) { return s->peer_fin ? 0 : UOS_EAGAIN; }
    unsigned char *p=data;
    for (unsigned i=0;i<count;++i) { p[i]=s->rx[s->rx_head];s->rx[s->rx_head++]=0;s->rx_head%=RX_BYTES; }
    s->rx_bytes-=count;s->ack=true;return count;
}
unsigned tcp_ready(uint64_t owner,uint64_t handle)
{
    const struct connection *s=lookup(owner,handle);
    if (!link.online || !s) { return UOS_EVENT_HANGUP; }
    if (s->phase==FAILED) { return UOS_EVENT_ERROR; }
    if (s->phase!=OPEN) { return 0; }
    unsigned ready=s->rx_bytes || s->peer_fin ? UOS_EVENT_READ : 0;
    return !s->tx_bytes ? ready|UOS_EVENT_WRITE : ready;
}
static void detach(struct connection *s,uint64_t now)
{
    if (s->phase!=OPEN) { memset(s,0,sizeof(*s));return; }
    s->owner=0;s->close_by=now+CLOSE_MS;s->rx_bytes=0;s->ack=true;memset(s->rx,0,sizeof(s->rx));
}
int64_t tcp_close(uint64_t owner,uint64_t handle,uint64_t now)
{
    struct connection *s=lookup(owner,handle);
    if (!s) { return UOS_EBADF; }detach(s,now);return 0;
}
void tcp_close_owner(uint64_t owner,uint64_t now)
{
    if (!owner) { return; }
    for (unsigned i=0;i<SLOT_COUNT;++i) { if (sockets[i].owner==owner) { detach(&sockets[i],now); } }
}
unsigned tcp_open_count(void)
{
    unsigned n=0;
    for (unsigned i=0;i<SLOT_COUNT;++i) { n+=sockets[i].owner!=0; }
    return n;
}
static void fail(struct connection *s,int64_t error)
{
    memset(s->tx,0,sizeof(s->tx));memset(s->rx,0,sizeof(s->rx));
    s->phase=s->owner ? FAILED : FREE;s->error=error;s->tx_bytes=s->rx_bytes=0;
    if (!s->owner) { memset(s,0,sizeof(*s)); }
}
static void receive_arp(const unsigned char *p)
{
    const unsigned char *a=p+14;
    if (net_be16(a)!=1 || net_be16(a+2)!=0x800 || a[4]!=6 || a[5]!=4 || net_be16(a+6)!=2 ||
        !net_equal(a+8,p+6,6) || !net_equal(a+18,link.mac,6) || !net_equal(a+24,link.ip,4)) { return; }
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        struct connection *s=&sockets[i];
        if (s->phase==ARP && s->tries && net_equal(a+14,s->peer.ipv4,4)) {
            memcpy(s->mac,a+8,6);s->phase=SYN;s->due=s->tries=0;
        }
    }
}
static unsigned peer_mss(const unsigned char *t,unsigned header)
{
    unsigned mss=UOS_TCP_MAX_BYTES;
    for (unsigned at=20;at<header;) {
        unsigned kind=t[at++];
        if (!kind) { break; }if (kind==1) { continue; }
        if (at==header || t[at]<2 || t[at]>header-at+1) { return 0; }
        if (kind==2) {
            if (t[at]!=4 || !net_be16(t+at+1)) { return 0; }
            mss=min(mss,net_be16(t+at+1));
        }
        at+=t[at]-1;
    }
    return mss;
}
static void acknowledge(struct connection *s,uint32_t ack)
{
    unsigned count=ack-s->una;s->una=ack;s->tries=0;s->due=0;
    if (s->fin_sent) { s->fin_acked=true;return; }
    for (unsigned i=count;i<s->tx_bytes;++i) { s->tx[i-count]=s->tx[i]; }
    memset(s->tx+s->tx_bytes-count,0,count);s->tx_bytes-=count;s->inflight-=count;
}
static void receive_segment(struct connection *s,const unsigned char *t,unsigned header,unsigned bytes)
{
    unsigned flags=t[13];uint32_t seq=net_be32(t+4),ack=net_be32(t+8);
    if (s->phase==SYN) {
        if (s->una==s->next || !(flags&TCP_ACK) || ack!=s->next) { return; }
        if (flags&TCP_RST) { fail(s,UOS_ECONNRESET);return; }
        unsigned mss=peer_mss(t,header);
        if (!(flags&TCP_SYN) || (flags&TCP_FIN) || bytes || !mss) { return; }
        s->phase=OPEN;s->una=ack;s->expect=seq+1;s->tries=0;s->due=0;s->mss=(uint16_t)mss;
        s->window=(uint16_t)net_be16(t+14);s->window_seq=seq;s->window_ack=ack;s->ack=true;return;
    }
    if (s->phase==TIME_WAIT) {
        if ((flags&(TCP_SYN|TCP_RST|TCP_FIN))==TCP_FIN && seq+bytes+1==s->expect) { s->ack=true; }
        return;
    }
    if (s->phase!=OPEN) { return; }
    if (flags&TCP_RST) {
        if (seq==s->expect) { fail(s,UOS_ECONNRESET); }
        else if (!before(seq,s->expect) && seq-s->expect<RX_BYTES-s->rx_bytes) { s->ack=true; }
        return;
    }
    if ((flags&TCP_SYN) || before(s->expect,seq) || before(s->next,ack)) { s->ack=true;return; }
    if (!(flags&TCP_ACK)) { return; }
    uint32_t end=seq+bytes+(unsigned)((flags&TCP_FIN)!=0);
    if (before(end,s->expect) || (end==s->expect && seq!=s->expect)) { s->ack=true;return; }
    if (before(s->una,ack)) { acknowledge(s,ack); }
    if (!before(ack,s->una) && (before(s->window_seq,seq) || (s->window_seq==seq && !before(ack,s->window_ack)))) {
        s->window=(uint16_t)net_be16(t+14);s->window_seq=seq;s->window_ack=ack;
    }
    if (!bytes && !(flags&TCP_FIN)) { return; }
    s->ack=true;
    if (s->peer_fin) { return; }
    unsigned skip=s->expect-seq;
    if (skip>bytes) { return; }
    unsigned accepted=min(bytes-skip,RX_BYTES-s->rx_bytes);
    if (s->owner) {
        for (unsigned i=0;i<accepted;++i) { s->rx[(s->rx_head+s->rx_bytes+i)%RX_BYTES]=t[header+skip+i]; }
        s->rx_bytes+=accepted;
    }
    s->expect+=accepted;
    if ((flags&TCP_FIN) && accepted==bytes-skip && s->rx_bytes<RX_BYTES) { ++s->expect;s->peer_fin=true; }
}
void tcp_receive(const void *frame,size_t n)
{
    const unsigned char *p=frame;
    if (!link.online || !p || n<42 || n>1514 || !net_equal(p,link.mac,6) || !mac_valid(p+6) || net_equal(p+6,link.mac,6)) { return; }
    unsigned type=net_be16(p+12);
    if (type==0x806) { receive_arp(p);return; }
    if (type!=0x800 || n<54) { return; }
    const unsigned char *v=p+14,*t=p+34;
    unsigned length=net_be16(v+2),header=(t[12]>>4)*4;
    if (v[0]!=0x45 || v[9]!=6 || !v[8] || length>n-14 || length<40 || header<20 || header>length-20 ||
        (net_be16(v+6)&~0x4000u) || !net_equal(v+16,link.ip,4) || net_checksum(v,20,0)) { return; }
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        struct connection *s=&sockets[i];
        if (s->phase>=SYN && s->phase<=TIME_WAIT && s->port==net_be16(t+2) && s->peer.port==net_be16(t) &&
            net_equal(s->mac,p+6,6) && net_equal(s->peer.ipv4,v+12,4)) {
            if (!net_transport_checksum(v,t,length-20,6)) { receive_segment(s,t,header,length-20-header); }
            return;
        }
    }
}

static void retry(struct connection *s,uint64_t now)
{ s->due=now+(1000u<<s->tries);++s->tries; }
static bool plan(struct connection *s,struct tcp_packet *packet,uint64_t now)
{
    if (s->phase==FREE || s->phase==FAILED) { return false; }
    if (s->phase==TIME_WAIT || s->close_by) {
        if (now>=s->close_by) { memset(s,0,sizeof(*s));return false; }
    }
    *packet=(struct tcp_packet){.local_mac=link.mac,.local_ip=link.ip,.peer_mac=s->mac,.peer_ip=s->peer.ipv4,
        .seq=s->next,.ack=s->expect,.local_port=s->port,.peer_port=s->peer.port,.window=(uint16_t)(RX_BYTES-s->rx_bytes),.flags=TCP_ACK};
    if (s->phase==ARP || s->phase==SYN) {
        if (now<s->due) { return false; }
        if (s->tries==(s->phase==ARP ? 3u : 4u)) { fail(s,s->phase==ARP ? UOS_EHOSTUNREACH : UOS_ETIMEDOUT);return false; }
        if (s->phase==ARP) { packet->peer_mac=NULL; }
        packet->flags=TCP_SYN;packet->seq=s->una;return true;
    }
    if (s->phase==OPEN && s->fin_acked && s->peer_fin && !s->ack) {
        s->phase=TIME_WAIT;s->close_by=now+TIME_WAIT_MS;return false;
    }
    if (s->phase==OPEN && now>=s->due && (s->tx_bytes || (!s->owner && !s->fin_acked))) {
        if (s->tries>=4) { fail(s,UOS_ETIMEDOUT);return false; }
        packet->seq=s->una;
        if (s->tx_bytes) {
            packet->payload=s->tx;
            unsigned room=s->window ? s->window : 1;
            packet->bytes=s->inflight ? min(s->inflight,room) : min(s->tx_bytes,min(s->mss,room));
            packet->flags|=TCP_PSH;
        } else if (s->window || s->fin_sent) { packet->flags|=TCP_FIN; }
        else { retry(s,now);return s->ack; }
        return true;
    }
    return s->ack;
}
enum block_result tcp_service(tcp_transmit tx,void *context,uint64_t now)
{
    if (!link.online || !tx) { return BLOCK_NOT_READY; }
    for (unsigned i=0;i<SLOT_COUNT;++i) {
        unsigned index=(link.next+i)%SLOT_COUNT;struct connection *s=&sockets[index];
        struct tcp_packet packet;
        if (!plan(s,&packet,now)) { continue; }
        link.next=(index+1)%SLOT_COUNT;
        enum block_result r=tx(context,&packet);
        if (r==BLOCK_NOT_READY) { return BLOCK_OK; }if (r!=BLOCK_OK) { return r; }
        if (s->phase==ARP) { s->due=now+1000;++s->tries; }
        else if (s->phase==SYN) { s->next=s->una+1;retry(s,now); }
        else if (s->phase==OPEN && (packet.bytes || (packet.flags&TCP_FIN))) {
            if (packet.flags&TCP_FIN) { s->fin_sent=true;s->next=packet.seq+1; }
            else if (!s->inflight) { s->inflight=(unsigned)packet.bytes;s->next=packet.seq+s->inflight; }
            retry(s,now);
        }
        if ((packet.flags&TCP_ACK) && packet.ack==s->expect && packet.window==RX_BYTES-s->rx_bytes) { s->ack=false; }
        return BLOCK_OK;
    }
    return BLOCK_OK;
}
