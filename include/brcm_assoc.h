#ifndef OS_BRCM_ASSOC_H
#define OS_BRCM_ASSOC_H
#include "brcm_sae_probe.h"
#include "brcm_control.h"
#include "wpa_rsn.h"
#include "wpa_probe.h"
#include "brcm_keys.h"
#include "dhcp.h"
#include "netprobe.h"
#include "network_session.h"
#define BRCM_ASSOC_IE_MAX 256
struct brcm_assoc_io {
    struct brcm_join_probe_io join;
    enum block_result (*identity)(void *,struct brcm_link_identity *,uint32_t *);
    /* Only the fresh, exclusively owned station may use a SET/clear journal
     * instead of interpreting firmware's synthesized wpaie GET as a snapshot. */
    bool fresh_ie_owner;
    unsigned request_fixed_bytes; /* Pi firmware assoc_info includes fixed 4 */
    bool key_probe;
    enum block_result (*data_tx)(void *,const void *,size_t);
    bool (*random)(void *,unsigned char output[32]);
    bool key_data_probe;const void *ap_rsn;size_t ap_rsn_bytes;
    bool install_probe;enum block_result (*key_set)(void *,const void *,size_t,uint32_t *);
    enum block_result (*m4_tx)(void *,const void *,size_t);
    bool dhcp_probe;enum block_result (*dhcp_tx)(void *,const void *,size_t);
    enum block_result (*peer_info)(void *,const unsigned char peer[6],struct brcm_station_info *,uint32_t *);
    enum block_result (*port_set)(void *,const unsigned char peer[6],bool authorize,uint32_t *);
    bool gateway_probe;enum block_result (*net_tx)(void *,enum netprobe_packet,unsigned,const void *,size_t);
    bool session_mode;int (*session_getc)(void *);void (*session_write)(void *,const char *);void (*session_info)(void *);
};
struct brcm_assoc_probe {
    struct brcm_sae_probe sae;struct brcm_auth_peer peer;
    bool attempted,ie_snapshot,ie_changed,ie_verified,ie_restored,armed,status_issued,status_ack,keys_erased;
    bool ie_owned,ie_baseline_ack,ie_set_ack,ie_clear_ack,ie_query_seen,assoc_ie_verified,request_h2e;
    enum block_result ie_query_result;uint32_t ie_query_status;size_t ie_query_bytes,assoc_ie_bytes;
    unsigned char ie_query_prefix[32],pmkid[16];
    unsigned assoc_query_phase;size_t assoc_info_reply_bytes,assoc_req_reply_bytes;uint32_t assoc_info_words[3];
    unsigned request_fixed_bytes;
    bool key_probe,m2_attempted,m2_written,m3_mic_verified,m3_unwrap_verified;
    unsigned key_phase,m2_bytes,m3_bytes,unwrapped_bytes;enum wpa_probe_result key_result;
    bool key_data_probe,key_data_verified,m4_encoded;unsigned m4_bytes;
    struct wpa_kde_info key_data;enum wpa_kde_result key_data_result;
    bool install_probe,m4_attempted,m4_written,post_m4_identity;unsigned settle_polls;
    struct brcm_key_journal keys;
    unsigned m3_duplicates,m3_retries_queued,m3_retries_verified,m4_writes,m4_deferred;
    bool m3_retry_pending;
    unsigned m3_retry_rejection;
    bool dhcp_probe;struct dhcp_client dhcp;
    bool gateway_probe,gateway_attempted,decline_active,decline_attempted,decline_written;
    enum block_result gateway_result,decline_result;uint64_t lease_origin;
    struct netprobe network;
    bool session_mode;struct network_session session;
    bool first_m4_before_keys,port_attempted,port_issued,port_ack,port_verified,port_close_active,port_closed;
    unsigned port_phase,port_queries,port_deferrals;
    enum block_result port_result,port_final_result,port_close_result;
    uint32_t port_status,port_final_status,port_close_status;
    struct brcm_station_info port_before,port_after,port_final;
    struct wpa_probe *private_exchange; /* synchronous only; erased before return */
    size_t request_saved,request_error_offset,request_rsn_offset;unsigned request_error,request_tag,request_rsn_bytes;
    struct wpa_rsn_info request_rsn;enum wpa_rsn_result request_rsn_error;
    unsigned char request_ies[512];
    bool ssid_event,link_event,assoc_event,identity_verified,policy_verified,association_verified,m1_seen;
    unsigned phase,polls,events,ignored,data_frames,data_ignored,data_bad,identity_queries;
    enum block_result error,event_error,ie_error,restore_error;
    uint32_t ie_status,auth_status,identity_status,last_event,last_status,last_reason;
    uint32_t failure_event,failure_status,failure_reason;
    uint16_t last_flags,key_info;unsigned char eapol_version,descriptor;
    unsigned char saved_ie[BRCM_ASSOC_IE_MAX];size_t saved_ie_bytes;
};
/* Pure fixed-profile serializers. auth_status contains PMKID, never PMK. */
enum block_result brcm_assoc_rsn(void *,size_t,size_t *);
enum block_result brcm_assoc_auth_status(const struct brcm_auth_peer *,const unsigned char pmkid[16],unsigned char output[60]);
/* Parse only the declared association-request IE bytes, not padded CDC data. */
enum block_result brcm_assoc_request_ies(const struct brcm_auth_peer *,const unsigned char pmkid[16],const void *,size_t,bool *h2e);
enum block_result brcm_assoc_request_check(const struct brcm_auth_peer *,const unsigned char pmkid[16],const void *,size_t,bool *h2e,struct brcm_assoc_probe *diagnostics);
/* Observe raw BCDC data / channel-1 events. No I/O or key operations here. */
void brcm_assoc_event(void *,const void *,size_t);
void brcm_assoc_data(void *,const void *,size_t);
/* Fresh committed SAE session required. Verified SAE -> firmware success ->
 * association identity/security checks -> optional M2/M3/KDE/offline M4 probe.
 * Initial live M4 precedes enabling TX keys; optional peer-port/DHCP test.
 * Always deauthorizes attempted ports, DOWN/deletes/restores/erases. */
enum block_result brcm_assoc_run(const struct brcm_assoc_io *,const struct brcm_auth_peer *,struct sae_session *,bool,struct brcm_assoc_probe *);
#endif
