#ifndef OS_BRCM_JOIN_PROBE_H
#define OS_BRCM_JOIN_PROBE_H
#include "brcm_auth.h"
struct brcm_join_probe {
    bool attempted,armed,up_attempted,down,join_issued,request_seen;
    bool live_mask_verified;unsigned live_read_mask,live_mismatch_mask;uint32_t live_policy[4];
    unsigned phase,events,ignored,malformed,polls;uint16_t request_flags;
    enum block_result error,event_error,cleanup_error;uint32_t firmware_status,cleanup_status;
    struct brcm_auth_peer peer;struct brcm_auth_probe policy;
};
struct brcm_join_probe_io {
    struct brcm_auth_probe_io policy;
    enum block_result (*station)(void *,uint32_t *); /* initialize idle non-AP to INFRA=1; retained this boot */
    enum block_result (*radio)(void *,bool,uint32_t *);
    enum block_result (*poll)(void *);
    enum block_result (*delay_us)(void *,unsigned);
    uint64_t (*milliseconds)(void *);
    /* Must disable subsequent I/O if interface DOWN cannot be confirmed. */
    void (*poison)(void *,enum block_result);
    enum block_result (*station_live)(void *,uint32_t *); /* read-only INFRA==1 after UP */
    /* Optional next stage, only after matched request + join SET ACK. It must
     * finish bounded work before returning; the join scope owns DOWN/restore. */
    enum block_result (*requested)(void *);
    uint32_t extra_events; /* additional event bits 0..31 for a scoped stage */
};
/* Fixed v0 ext_join, one specific BSSID and 2.4 GHz 20 MHz channel, no
 * wildcard/fallback. Pure serializer; output is 70 bytes. */
enum block_result brcm_join_encode(const struct brcm_auth_peer *,unsigned char output[70]);
/* Bind to control.event only for this synchronous one-shot call. Request
 * events may arrive before the join SET's acknowledgement. The default
 * requested=NULL path observes only; optional work remains in the same scope. */
void brcm_join_event(void *,const void *,size_t);
enum block_result brcm_join_probe_run(const struct brcm_join_probe_io *,const struct brcm_auth_peer *,bool,struct brcm_join_probe *);
#endif
