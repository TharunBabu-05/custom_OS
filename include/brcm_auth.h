#ifndef OS_BRCM_AUTH_H
#define OS_BRCM_AUTH_H
#include "block.h"
#define BRCM_AUTH_PARAMS_MAX 136
/* event_msgs_ext encodes its complete mask length in one byte. */
#define BRCM_AUTH_MASK_MAX 255
struct brcm_auth_peer { unsigned char local[6],peer[6],ssid[32];uint8_t ssid_length,channel,band;bool hunting; };
enum brcm_auth_kind { BRCM_AUTH_REQUEST=187,BRCM_AUTH_RX=188,BRCM_AUTH_TX=189,BRCM_AUTH_OFFCHANNEL=190 };
enum brcm_auth_filter { AUTH_FILTER_NONE,AUTH_FILTER_INTERFACE,AUTH_FILTER_PEER,AUTH_FILTER_SSID,AUTH_FILTER_CHANNEL,AUTH_FILTER_COOKIE };
struct brcm_auth_message {
    enum brcm_auth_kind kind;uint32_t status,reason,packet_id;
    uint16_t sequence,auth_status,request_flags;size_t body_bytes;unsigned char body[98];
    uint16_t rx_metadata_version,wire_group;size_t wire_body_bytes;
    /* Filtered returns leave kind/body zero. Envelope-only diagnostics do
     * not establish identity or authentication; event 0 is a valid code. */
    bool envelope_valid;uint32_t wire_event,wire_status,wire_bytes,wire_cookie;enum brcm_auth_filter filter;
    unsigned char wire_interface,wire_bss;
    uint16_t wire_flags;uint32_t wire_reason;unsigned char wire_peer[6];
};
/* Serialize mgmt_frame v0 parameters, including the six-byte SAE auth header.
 * No I/O. Only fixed group-19 commits / ordinary confirms. Commit status is
 * 126 for H2E, 0 for hunting-and-pecking; peer mode is fixed before TX. */
enum block_result brcm_auth_encode(const struct brcm_auth_peer *,uint32_t packet_id,unsigned sequence,const void *body,size_t bytes,void *output,size_t capacity,size_t *actual);
/* Parse a complete channel-1 BCDC/event payload. RX identity is taken from
 * the firmware event envelope; firmware may replace the raw MAC header.
 * BLOCK_NOT_READY means unrelated event/peer/packet ID; no queue is retained. */
enum block_result brcm_auth_event(const struct brcm_auth_peer *,uint32_t packet_id,const void *,size_t,struct brcm_auth_message *);
const char *brcm_auth_error(enum block_result);
struct brcm_auth_probe_io {
    void *context;
    enum block_result (*iovar)(void *,bool,const char *,const void *,size_t,void *,size_t,size_t *,uint32_t *);
};
struct brcm_auth_probe {
    bool attempted,changed,verified,restored,mask_compatibility,mask_legacy_verified,mask_snapshot_stable;enum block_result error,restore_error;
    unsigned phase,property,mask_bytes,observed_mask,mask_reply_bytes,mismatch_mask,auth_trace_mask;
    unsigned failure_phase,failure_property,restore_property;
    bool mask_readback_verified;uint32_t firmware_status,restore_status;
    bool auth_readback_pending,external_request_verified;
    uint32_t extra_events; /* scoped low event bits, in addition to SAE 187..190 */
    uint32_t saved[4],observed[4],auth_after[4];unsigned char mask[BRCM_AUTH_MASK_MAX],mask_header[4];
};
/* Interface must already be DOWN after a clean scan. Snapshot, set/read back
 * auth=3/wpa_auth=SAE/wsec=AES/mfp=required and events 187..190, then restore.
 * No interface UP, join, credential or management-frame commands are issued. */
enum block_result brcm_auth_probe_run(const struct brcm_auth_probe_io *,bool prerequisites,struct brcm_auth_probe *);
/* Internal scoped action: all non-auth controls must verify. An acknowledged
 * auth=3 SET with auth=2 GET may reach ONLY an external-request probe, whose
 * callback must independently observe a matching SAE request and stop radio
 * activity before returning OK. Never an authentication/key-export API. */
enum block_result brcm_auth_policy_action(const struct brcm_auth_probe_io *,bool,struct brcm_auth_probe *,enum block_result (*)(void *),void *);
enum block_result brcm_auth_policy_events(const struct brcm_auth_probe_io *,bool,struct brcm_auth_probe *,uint32_t extra_events,enum block_result (*)(void *),void *);
#endif
