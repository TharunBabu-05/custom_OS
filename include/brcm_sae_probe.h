#ifndef OS_BRCM_SAE_PROBE_H
#define OS_BRCM_SAE_PROBE_H
#include "brcm_join_probe.h"
#include "sae_session.h"
#define BRCM_SAE_TRACE_MAX 12
#define BRCM_SAE_COMMIT_ATTEMPTS 3
struct brcm_sae_event_trace {
    uint32_t event,status,bytes,cookie;enum brcm_auth_filter filter;enum block_result result;
    unsigned char stage,interface,bss; /* 0 before TX, 1 commit, 2 confirm, 3 handoff */
};
struct brcm_sae_handoff {
    void *context;
    /* Called synchronously ONLY after own SET/TX ACK and peer HMAC verify.
     * It may perform bounded I/O. The outer scope still owns DOWN/restore. */
    enum block_result (*verified)(void *,struct sae_session *,const struct brcm_auth_peer *);
};
struct brcm_sae_probe {
    struct brcm_join_probe join;
    bool attempted,tx_attempted,tx_set_acknowledged,tx_status_seen,offchannel_seen,rx_seen,rx_structural,peer_valid,reflection,erased;
    unsigned phase,events,ignored,polls;uint32_t packet_id,firmware_status,tx_status,offchannel_status;
    uint16_t sequence,auth_status,group,metadata_version;size_t wire_body_bytes;
    enum block_result error,event_error,rx_result;
    unsigned char own_commit[98],peer_commit[98]; /* public wire values; erased after probe */
    bool confirm_mode,confirm_tx_attempted,confirm_set_acknowledged,confirm_tx_status_seen,confirm_rx_seen,confirmed,crypto_attempted;
    uint32_t confirm_tx_status,confirm_firmware_status;uint64_t key_math_ms;
    enum sae_result crypto_result;struct sae_session *session; /* only during synchronous auth call */
    unsigned char own_confirm[34],peer_confirm[34]; /* public HMAC transcripts, erased too */
    unsigned commit_attempts,early_events,rx_early,rx_filtered,rx_bad,wire_auth_rx,trace_count;
    uint32_t active_packet_id;struct brcm_sae_event_trace trace[BRCM_SAE_TRACE_MAX];
    const struct brcm_sae_handoff *handoff;bool handoff_attempted;
};
void brcm_sae_probe_event(void *,const void *,size_t);
/* One precomputed fresh group-19 commit only. Source RNG and credential
 * handling belong to the caller. No confirms, key derivation/install or
 * external-auth success. This scope owns join, DOWN, policy restoration and
 * transcript erasure. Peer math is checked only after radio is DOWN. */
enum block_result brcm_sae_probe_run(const struct brcm_join_probe_io *,const struct brcm_auth_peer *,const unsigned char commit[98],uint32_t packet_id,bool prerequisites,struct brcm_sae_probe *);
/* Bounded live commit + confirm test: at most three identical commit TXs
 * after ACK/no-RX timeouts, then one confirm. Takes a freshly COMMITTED session,
 * retains it only for this call, and destroys it on every attempted exit.
 * No keys exported or external-auth success sent to firmware. Even success
 * leaves the interface DOWN, with no association/data path. */
enum block_result brcm_sae_auth_run(const struct brcm_join_probe_io *,const struct brcm_auth_peer *,struct sae_session *,uint32_t packet_id,bool prerequisites,struct brcm_sae_probe *);
enum block_result brcm_sae_handoff_run(const struct brcm_join_probe_io *,const struct brcm_auth_peer *,struct sae_session *,uint32_t packet_id,bool prerequisites,const struct brcm_sae_handoff *,struct brcm_sae_probe *);
#endif
