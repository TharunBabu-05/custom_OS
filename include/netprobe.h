#ifndef OS_NETPROBE_H
#define OS_NETPROBE_H
#include "dhcp.h"
enum netprobe_phase { NP_IDLE,NP_PROBING,NP_ANNOUNCING,NP_RESOLVING,NP_PING,NP_DONE,NP_READY,NP_FAILED };
enum netprobe_packet { NP_ARP_PROBE,NP_ARP_ANNOUNCE,NP_ARP_RESOLVE,NP_ARP_REPLY,NP_ECHO };
struct netprobe {
    enum netprobe_phase phase;enum block_result error;
    bool attempted,armed,active,was_active,conflict,gateway_known,gateway_changed,reply_pending,resolve_issued,live;
    unsigned char mac[6],gateway_mac[6],token[16],reply_mac[6],reply_ip[4],conflict_mac[6];
    struct dhcp_lease lease;unsigned identifier,initial_delay,probe_delay[2];
    unsigned char target[4];uint64_t ping_started,ping_next,arp_window;unsigned ping_finished,arp_window_replies;
    enum block_result ping_result;
    uint64_t lease_origin,start,echo_started[3],latency_ms[3];
    unsigned probes,announcements,resolutions,echoes,arp_replies,reply_queued,reply_dropped;
    unsigned issued_mask,written_mask,seen_mask,received_mask,received,ignored,malformed,arp_rx,ipv4_rx,deferrals,polls;
};
struct netprobe_io {
    void *context;enum block_result (*tx)(void *,enum netprobe_packet,unsigned,const void *,size_t);
    enum block_result (*poll)(void *);enum block_result (*delay_us)(void *,unsigned);
    uint64_t (*milliseconds)(void *);bool (*ready)(void *);
};
enum block_result netprobe_init(struct netprobe *,const unsigned char mac[6],const struct dhcp_lease *,const unsigned char entropy[32],uint64_t lease_origin);
enum block_result netprobe_encode(const struct netprobe *,enum netprobe_packet,unsigned sequence,void *,size_t,size_t *);
bool netprobe_packet_valid(const struct netprobe *,enum netprobe_packet,unsigned sequence,const void *,size_t);
bool netprobe_can_tx(const struct netprobe *,uint64_t now);
bool netprobe_selftest(void);
/* Pure receive callback, no clock reads, packet writes or other I/O. */
void netprobe_receive(struct netprobe *,const void *,size_t);
enum block_result netprobe_run(const struct netprobe_io *,const unsigned char mac[6],const struct dhcp_lease *,const unsigned char entropy[32],uint64_t lease_origin,struct netprobe *);
/* Open retains its verified temporary address for the foreground session.
 * Close is mandatory on every exit. No background task or lease renewal. */
enum block_result netprobe_open(const struct netprobe_io *,const unsigned char mac[6],const struct dhcp_lease *,const unsigned char entropy[32],uint64_t lease_origin,struct netprobe *);
enum block_result netprobe_ping_start(struct netprobe *,const unsigned char target[4],const unsigned char entropy[32],uint64_t now);
enum block_result netprobe_service(const struct netprobe_io *,struct netprobe *);
void netprobe_close(struct netprobe *);
#endif
