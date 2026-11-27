/* Emulator-only peer for the actual EL0 socket ABI and worker scheduler. */
#include <string.h>
#include "network_worker.h"
#include "process.h"
#include "cpu.h"
#include "udp.h"
#include "tcp.h"

static const unsigned char local_mac[]={2,0,0,0,0,1},peer_mac[]={2,0,0,0,0,2};
static const unsigned char local_ip[]={192,0,2,1},mask[]={255,255,255,0};
struct udp_test_peer { unsigned char frame[UDP_FRAME_MAX];size_t bytes;unsigned probes;bool lost; };
static void reply_arp(unsigned char *p,const unsigned char *mac,const unsigned char *ip,const unsigned char *target)
{
    const unsigned char arp[]={0x08,0x06,0,1,0x08,0,6,4,0,2};
    memset(p,0,60);memcpy(p,mac,6);memcpy(p+6,peer_mac,6);memcpy(p+12,arp,sizeof(arp));
    memcpy(p+22,peer_mac,6);memcpy(p+28,target,4);memcpy(p+32,mac,6);memcpy(p+38,ip,4);
}

static enum block_result echo_tx(void *context,const struct udp_packet *packet)
{
    struct udp_test_peer *peer=context;
    if (!packet->peer_mac) {
        reply_arp(peer->frame,packet->local_mac,packet->local_ip,packet->peer_ip);peer->bytes=60;
    } else if (packet->peer_port==32002) {
        peer->lost=true;
    } else {
        unsigned char mismatch[16];
        const void *payload=packet->payload;
        if (packet->peer_port==32003 || packet->peer_port==32004) {
            if (packet->bytes!=sizeof(mismatch)) { return BLOCK_IO; }
            ++peer->probes;
            if (packet->peer_port==32004 || peer->probes==1) { return BLOCK_OK; }
            if (peer->probes==2) {
                memcpy(mismatch,payload,sizeof(mismatch));
                mismatch[15]^=1;
                payload=mismatch;
            }
        }
        const struct udp_packet reply={peer_mac,packet->peer_ip,packet->local_mac,packet->local_ip,
            packet->peer_port,packet->local_port,payload,packet->bytes};
        if (!udp_encode(&reply,peer->frame,sizeof(peer->frame),&peer->bytes)) { return BLOCK_IO; }
    }
    return BLOCK_OK;
}
static void test_link(void *context)
{
    struct udp_test_peer *peer=context;
    if (!udp_link_up(local_mac,local_ip,mask)) { return; }
    while (!peer->lost && network_worker_pause()) {
        uint64_t now=timer_ticks()*1000/timer_frequency();
        if (peer->bytes) { udp_receive(peer->frame,peer->bytes,now);peer->bytes=0; }
        if (udp_service(echo_tx,peer,now)!=BLOCK_OK) { break; }
    }
    udp_link_down();
}
static bool test_app(const char *command,bool loss,int status,unsigned probes)
{
    struct udp_test_peer peer={0};struct app_args args;
    if (!app_args_parse(command,&args) || !network_worker_start(test_link,&peer)) { return false; }
    bool ok=process_terminal(&args,probes ? 8 : 4)==status && peer.lost==loss && peer.probes==probes && !udp_open_count();
    return network_worker_stop() && ok;
}
bool udp_worker_test(void)
{
    return test_app("/boot/UDPCHK.ELF model",false,0,0) &&
           test_app("/boot/UDPCHK.ELF loss",true,0,0) &&
           test_app("/boot/UDPCHK.ELF waitloss",true,0,0) &&
           test_app("/boot/UDPCHK.ELF probe 192.0.2.2 32003 3",false,0,5) &&
           test_app("/boot/UDPCHK.ELF probe 192.0.2.2 32004 1",false,1,3);
}

struct tcp_test_peer {
    unsigned char frame[TCP_FRAME_MAX];size_t bytes;
    uint64_t now,reply_at,first_data,last_data;
    unsigned delay_ms;
    uint32_t seq,expect;unsigned target,echoed,syns;bool fin,client_first;
};
static enum block_result tcp_echo_tx(void *context,const struct tcp_packet *p)
{
    struct tcp_test_peer *peer=context;
    if (!tcp_packet_pending(p)) { return BLOCK_IO; }
    if (!p->peer_mac) {
        reply_arp(peer->frame,p->local_mac,p->local_ip,p->peer_ip);peer->bytes=60;return BLOCK_OK;
    }
    struct tcp_packet reply={.local_mac=peer_mac,.local_ip=p->peer_ip,.peer_mac=p->local_mac,.peer_ip=p->local_ip,
        .local_port=p->peer_port,.peer_port=p->local_port,.window=1024,.flags=TCP_ACK,.seq=peer->seq,.ack=peer->expect};
    if (p->flags&TCP_SYN) {
        if (++peer->syns==1) { return BLOCK_OK; } /* Lose the first SYN. */
        if (peer->syns!=2) { return BLOCK_IO; }
        reply.seq=1000;reply.ack=peer->expect=p->seq+1;peer->seq=1001;reply.flags|=TCP_SYN;
    } else if (p->bytes) {
        if (p->seq!=peer->expect || p->ack!=peer->seq || peer->fin) { return BLOCK_IO; }
        if (!peer->echoed) { peer->first_data=peer->now; }
        peer->last_data=peer->now;
        peer->reply_at=peer->now+peer->delay_ms;
        peer->expect+=(uint32_t)p->bytes;peer->seq+=(uint32_t)p->bytes;peer->echoed+=(unsigned)p->bytes;
        reply.ack=peer->expect;reply.payload=p->payload;reply.bytes=p->bytes;reply.flags|=TCP_PSH;
        if (peer->echoed==peer->target && !peer->client_first) { reply.flags|=TCP_FIN;++peer->seq;peer->fin=true; }
    } else if (p->flags&TCP_FIN) {
        if (peer->echoed!=peer->target || p->seq!=peer->expect || p->ack!=peer->seq) { return BLOCK_IO; }
        reply.ack=++peer->expect;
        if (peer->client_first) {
            static const char trailer[]="ouros-fin-ok";
            reply.payload=trailer;reply.bytes=sizeof(trailer)-1;reply.flags|=TCP_PSH|TCP_FIN;
            peer->seq+=(uint32_t)reply.bytes+1;peer->fin=true;
        } else if (!peer->fin) { return BLOCK_IO; }
    } else { return BLOCK_OK; }
    return tcp_encode(&reply,peer->frame,sizeof(peer->frame),&peer->bytes) ? BLOCK_OK : BLOCK_IO;
}
static void tcp_test_link(void *context)
{
    struct tcp_test_peer *peer=context;const unsigned char secret[32]={3};
    if (!tcp_link_up(local_mac,local_ip,mask,secret)) { return; }
    while (network_worker_pause()) {
        peer->now=timer_ticks()*1000/timer_frequency();
        if (peer->bytes) {
            if (peer->now<peer->reply_at) { continue; }
            tcp_receive(peer->frame,peer->bytes);peer->bytes=0;peer->reply_at=0;
        }
        if (tcp_service(tcp_echo_tx,peer,peer->now)!=BLOCK_OK) { break; }
    }
    tcp_link_down();
}
static bool test_tcp_app(const char *command,unsigned bytes,bool client_first)
{
    struct tcp_test_peer peer={.target=bytes,.client_first=client_first};struct app_args args;
    if (!app_args_parse(command,&args) || !network_worker_start(tcp_test_link,&peer)) { return false; }
    bool ok=process_terminal(&args,8)==0 && peer.fin && peer.echoed==bytes && peer.syns==2 && !tcp_open_count();
    return network_worker_stop() && ok;
}
bool tcp_worker_test(void)
{
    return test_tcp_app("/boot/TCPCHK.ELF model",5,false) &&
           test_tcp_app("/boot/TCPCHK.ELF 192.0.2.2 34002",16384,false) &&
           test_tcp_app("/boot/TLSECHO.ELF transport-check",700,false) &&
           test_tcp_app("/boot/TCPCHK.ELF 192.0.2.2 34004 half",16384,true);
}

bool tcp_worker_slow_test(void)
{
    struct tcp_test_peer peer={.target=16384,.client_first=true,.delay_ms=350};
    struct app_args args;
    if (!app_args_parse("/boot/SHELL.ELF",&args) || !network_worker_start(tcp_test_link,&peer)) { return false; }
    bool ok=process_terminal(&args,35)==0 && peer.fin && peer.echoed==peer.target &&
            peer.last_data-peer.first_data>10000 && !tcp_open_count();
    return network_worker_stop() && ok;
}
