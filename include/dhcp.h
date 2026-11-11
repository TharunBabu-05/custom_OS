#ifndef OS_DHCP_H
#define OS_DHCP_H
#include "block.h"
#define DHCP_FRAME_BYTES 342
enum dhcp_phase { DHCP_IDLE,DHCP_SELECTING,DHCP_REQUESTING,DHCP_ACKED,DHCP_FAILED };
struct dhcp_lease { unsigned char address[4],mask[4],router[4],dns[4],server[4];uint32_t seconds;bool have_mask,have_router,have_dns; };
/* Public metadata only. An ACK is an observed lease, not an installed IP
 * address: this probe has no ARP conflict detection, routing or renewal. */
struct dhcp_client {
    enum dhcp_phase phase;enum block_result error;
    bool attempted,armed,discover_issued,discover_written,request_issued,request_written,have_offer,ack_seen,nak_seen;
    unsigned char mac[6];uint32_t xid;struct dhcp_lease offer,lease;
    unsigned discover_sent,request_sent,received,ignored,malformed,deferrals,polls;
    unsigned initial_delay_ms,retry_ms[2];
};
struct dhcp_io {
    void *context;enum block_result (*tx)(void *,const void *,size_t);
    enum block_result (*poll)(void *);enum block_result (*delay_us)(void *,unsigned);
    uint64_t (*milliseconds)(void *);bool (*ready)(void *);
};
enum block_result dhcp_client_init(struct dhcp_client *,const unsigned char mac[6],const unsigned char entropy[10]);
enum block_result dhcp_encode(const struct dhcp_client *,bool request,unsigned seconds,void *,size_t,size_t *);
/* Only after an ACKed lease is found conflicting; caller gates live use. */
enum block_result dhcp_encode_decline(const struct dhcp_client *,void *,size_t,size_t *);
/* Ethernet receive only: validates/copies, never transmits or calls I/O. */
void dhcp_receive(struct dhcp_client *,const void *,size_t);
enum block_result dhcp_probe_run(const struct dhcp_io *,const unsigned char mac[6],const unsigned char entropy[10],struct dhcp_client *);
bool dhcp_selftest(void);
/* Typed transport guard; no arbitrary IPv4 or UDP TX entry point. */
bool dhcp_wire_request_valid(const void *,size_t);
#endif
