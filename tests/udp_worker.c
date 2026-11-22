/* Emulator-only peer for the actual EL0 socket ABI and worker scheduler. */
#include <string.h>
#include "network_worker.h"
#include "process.h"
#include "cpu.h"
#include "udp.h"

static const unsigned char local_mac[]={2,0,0,0,0,1},peer_mac[]={2,0,0,0,0,2};
static const unsigned char local_ip[]={192,0,2,1},mask[]={255,255,255,0};
struct udp_test_peer { unsigned char frame[UDP_FRAME_MAX];size_t bytes;bool lost; };

static enum block_result echo_tx(void *context,const struct udp_packet *packet)
{
    struct udp_test_peer *peer=context;
    if (!packet->peer_mac) {
        const unsigned char arp[]={0x08,0x06,0,1,0x08,0,6,4,0,2};
        unsigned char *p=peer->frame;memset(p,0,60);
        memcpy(p,packet->local_mac,6);memcpy(p+6,peer_mac,6);memcpy(p+12,arp,sizeof(arp));
        memcpy(p+22,peer_mac,6);memcpy(p+28,packet->peer_ip,4);
        memcpy(p+32,packet->local_mac,6);memcpy(p+38,packet->local_ip,4);peer->bytes=60;
    } else if (packet->peer_port==32002) {
        peer->lost=true;
    } else {
        const struct udp_packet reply={peer_mac,packet->peer_ip,packet->local_mac,packet->local_ip,
            packet->peer_port,packet->local_port,packet->payload,packet->bytes};
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
static bool test_app(const char *command,bool loss)
{
    struct udp_test_peer peer={0};struct app_args args;
    if (!app_args_parse(command,&args) || !network_worker_start(test_link,&peer)) { return false; }
    bool ok=process_terminal(&args,4)==0 && peer.lost==loss && !udp_open_count();
    return network_worker_stop() && ok;
}
bool udp_worker_test(void)
{ return test_app("/boot/UDPCHK.ELF model",false) && test_app("/boot/UDPCHK.ELF loss",true); }
