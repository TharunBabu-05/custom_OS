#include "brcm_sae_probe.h"
#include "p256.h"
#include "crypto.h"
static bool valid_commit(const unsigned char *p)
{
    unsigned high=0;for (unsigned i=2;i<33;++i) { high|=p[i]; }
    return p[0]==19 && !p[1] && p256_scalar_valid(p+2) && (high || p[33]>1) && p256_point_valid(p+34);
}
void brcm_sae_probe_event(void *context,const void *payload,size_t bytes)
{
    struct brcm_sae_probe *s=context;if (!s) { return; }
    brcm_join_event(&s->join,payload,bytes);
    if (!s->join.armed || s->event_error!=BLOCK_OK) { return; }
    bool early=!s->tx_attempted;
    if ((early ? ++s->early_events : ++s->events)>256) { s->event_error=BLOCK_RANGE;return; }
    struct brcm_auth_message m;enum block_result r=brcm_auth_event(&s->join.peer,s->active_packet_id,payload,bytes,&m);
    if (m.envelope_valid) {
        struct brcm_sae_event_trace *t=&s->trace[s->trace_count++%BRCM_SAE_TRACE_MAX];
        *t=(struct brcm_sae_event_trace){.event=m.wire_event,.status=m.wire_status,.bytes=m.wire_bytes,.cookie=m.wire_cookie,.filter=m.filter,.result=r,
            .stage=early ? 0 : s->handoff_attempted ? 3 : s->confirm_tx_attempted ? 2 : 1,.interface=m.wire_interface,.bss=m.wire_bss};
        if (m.wire_event==BRCM_AUTH_RX) {
            ++s->wire_auth_rx;if (early) { ++s->rx_early; }
            if (m.filter) { ++s->rx_filtered; }else if (r!=BLOCK_OK) { ++s->rx_bad; }
        }
    }
    /* Keep observing early/filtered envelopes without accepting them as a
     * response to this commit. No I/O, curve math or secret bytes in trace. */
    if (early) { return; }
    if (m.kind==BRCM_AUTH_RX) {
        if (r==BLOCK_OK && m.sequence==2 && s->confirm_mode && s->rx_structural) {
            if (s->confirm_rx_seen) { ++s->ignored;return; }
            for (unsigned i=0;i<34;++i) { s->peer_confirm[i]=m.body[i]; }s->confirm_rx_seen=true;return;
        }
        if (s->rx_seen) {
            if (s->confirm_mode && (r!=BLOCK_OK || m.sequence!=1 || m.body_bytes!=98 || !crypto_equal(m.body,s->peer_commit,98))) { s->event_error=BLOCK_UNSUPPORTED; }
            else { ++s->ignored; }return;
        }
        s->rx_seen=true;s->rx_result=r;s->sequence=m.sequence;s->auth_status=m.auth_status;s->group=m.wire_group;
        s->metadata_version=m.rx_metadata_version;s->wire_body_bytes=m.wire_body_bytes;
        if (r==BLOCK_OK && m.sequence==1 && m.auth_status==(s->join.peer.hunting ? 0 : 126) && m.body_bytes==98 && m.wire_group==19) {
            for (unsigned i=0;i<98;++i) { s->peer_commit[i]=m.body[i]; }s->rx_structural=true;
        }else { s->event_error=r==BLOCK_OK ? BLOCK_UNSUPPORTED : r; }
        return;
    }
    if (r!=BLOCK_OK) { ++s->ignored;return; }
    if (m.kind==BRCM_AUTH_TX) {
        if (s->confirm_tx_attempted) { s->confirm_tx_status_seen=true;s->confirm_tx_status=m.status; }
        else { s->tx_status_seen=true;s->tx_status=m.status; }if (m.status) { s->event_error=BLOCK_IO; }
    }
    else if (m.kind==BRCM_AUTH_OFFCHANNEL) { s->offchannel_seen=true;s->offchannel_status=m.status;if (m.status) { s->event_error=BLOCK_IO; } }
    else { ++s->ignored; }
}
struct adapter { const struct brcm_join_probe_io *base;struct brcm_sae_probe *s; };
static enum block_result iovar(void *ctx,bool set,const char *name,const void *in,size_t bytes,void *out,size_t cap,size_t *actual,uint32_t *status)
{ struct adapter *a=ctx;return a->base->policy.iovar(a->base->policy.context,set,name,in,bytes,out,cap,actual,status); }
static enum block_result station(void *ctx,uint32_t *status)
{ struct adapter *a=ctx;return a->base->station(a->base->policy.context,status); }
static enum block_result live(void *ctx,uint32_t *status)
{ struct adapter *a=ctx;return a->base->station_live(a->base->policy.context,status); }
static enum block_result radio(void *ctx,bool up,uint32_t *status)
{ struct adapter *a=ctx;return a->base->radio(a->base->policy.context,up,status); }
static enum block_result poll(void *ctx)
{ struct adapter *a=ctx;return a->base->poll(a->base->policy.context); }
static enum block_result delay(void *ctx,unsigned us)
{ struct adapter *a=ctx;return a->base->delay_us(a->base->policy.context,us); }
static uint64_t milliseconds(void *ctx)
{ struct adapter *a=ctx;return a->base->milliseconds(a->base->policy.context); }
static void poison(void *ctx,enum block_result r)
{ struct adapter *a=ctx;a->base->poison(a->base->policy.context,r); }
static bool complete(const struct brcm_sae_probe *s,bool confirm)
{ return confirm ? s->confirm_tx_status_seen && s->confirm_rx_seen : s->tx_status_seen && s->rx_structural; }
static enum block_result wait_reply(void *ctx,bool confirm)
{
    struct brcm_sae_probe *s=((struct adapter *)ctx)->s;uint64_t start=milliseconds(ctx);enum block_result r;
    for (unsigned i=0;i<500 && s->event_error==BLOCK_OK && !complete(s,confirm);++i) {
        if (milliseconds(ctx)-start>=5000) { break; }
        ++s->polls;r=poll(ctx);if (r!=BLOCK_OK) { return r; }
        if (s->event_error!=BLOCK_OK || complete(s,confirm)) { break; }
        r=delay(ctx,10000);if (r!=BLOCK_OK) { return r; }
    }
    if (s->event_error!=BLOCK_OK) { return s->event_error; }
    return complete(s,confirm) ? BLOCK_OK : BLOCK_TIMEOUT;
}
static enum block_result transmit(void *ctx)
{
    struct adapter *a=ctx;struct brcm_sae_probe *s=a->s;
    unsigned char params[BRCM_AUTH_PARAMS_MAX];size_t n=0;s->phase=1;enum block_result r;
    if (s->event_error!=BLOCK_OK) { return s->event_error; }
    unsigned attempts=s->confirm_mode ? BRCM_SAE_COMMIT_ATTEMPTS : 1;
    for (unsigned attempt=0;attempt<attempts;++attempt) {
        s->active_packet_id=s->packet_id+attempt;s->tx_status_seen=false;s->tx_status=0;s->tx_set_acknowledged=false;
        r=brcm_auth_encode(&s->join.peer,s->active_packet_id,1,s->own_commit,98,params,sizeof(params),&n);
        if (r!=BLOCK_OK) { crypto_wipe(params,sizeof(params));return r; }
        s->phase=2;s->tx_attempted=true;++s->commit_attempts;
        r=iovar(ctx,true,"mgmt_frame",params,n,NULL,0,&n,&s->firmware_status);crypto_wipe(params,sizeof(params));
        if (r!=BLOCK_OK) { return r; }s->tx_set_acknowledged=true;s->phase=3;
        uint64_t started=milliseconds(ctx);r=wait_reply(ctx,false);
        if (r==BLOCK_OK) { break; }
        /* A retry is the EXACT same commit/session, not a fresh scalar/mask.
         * Only ACKed TX with no matched RX may retry, after the full 5-second
         * wait (beyond the 4-second dwell). A stalled clock, NACK, missing ACK,
         * malformed/rejected RX or I/O failure must stop, not loop or weaken. */
        if (r!=BLOCK_TIMEOUT || !s->tx_status_seen || s->tx_status || s->rx_seen || s->event_error!=BLOCK_OK || milliseconds(ctx)-started<5000) { return r; }
    }
    if (r!=BLOCK_OK) { return r; }s->phase=4;
    if (!s->confirm_mode) { return BLOCK_OK; }
    /* Heavy work is outside callbacks. Private session is destroyed by this
     * synchronous scope; no keys are exported or installed. */
    s->reflection=crypto_equal(s->own_commit+2,s->peer_commit+2,96);
    s->peer_valid=!s->reflection && valid_commit(s->peer_commit);if (!s->peer_valid) { return BLOCK_RANGE; }
    s->phase=7;s->crypto_attempted=true;uint64_t start=milliseconds(ctx);
    s->crypto_result=sae_session_peer(s->session,s->peer_commit,98);
    if (s->crypto_result==SAE_OK) { s->crypto_result=sae_session_confirm(s->session,s->own_confirm); }
    s->key_math_ms=milliseconds(ctx)-start;
    if (s->crypto_result!=SAE_OK) { return BLOCK_IO; }
    if (s->key_math_ms>=4000) { return BLOCK_TIMEOUT; } /* do not send a known-late confirm */
    ++s->active_packet_id;r=brcm_auth_encode(&s->join.peer,s->active_packet_id,2,s->own_confirm,34,params,sizeof(params),&n);
    if (r!=BLOCK_OK) { crypto_wipe(params,sizeof(params));return r; }
    s->phase=8;s->confirm_tx_attempted=true;
    r=iovar(ctx,true,"mgmt_frame",params,n,NULL,0,&n,&s->confirm_firmware_status);crypto_wipe(params,sizeof(params));
    if (r!=BLOCK_OK) { return r; }s->confirm_set_acknowledged=true;
    r=wait_reply(ctx,true);if (r!=BLOCK_OK) { return r; }
    s->phase=9;s->crypto_result=sae_session_verify(s->session,s->peer_confirm,34);
    s->confirmed=s->crypto_result==SAE_OK;if (!s->confirmed) { return BLOCK_IO; }
    if (s->handoff) { s->handoff_attempted=true;s->phase=11;return s->handoff->verified(s->handoff->context,s->session,&s->join.peer); }
    return BLOCK_OK;
}
static enum block_result run(const struct brcm_join_probe_io *io,const struct brcm_auth_peer *peer,const unsigned char commit[98],struct sae_session *session,uint32_t packet_id,bool prerequisites,const struct brcm_sae_handoff *handoff,struct brcm_sae_probe *s)
{
    if (!s || !io || !io->policy.iovar || !io->station || !io->station_live || !io->radio || !io->poll || !io->delay_us || !io->milliseconds || !io->poison || io->requested || !commit || !packet_id || (session && packet_id>UINT32_MAX-BRCM_SAE_COMMIT_ATTEMPTS) || (handoff && (!session || !handoff->verified))) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }if (!prerequisites) { return BLOCK_NOT_READY; }
    unsigned char check[70];enum block_result r=brcm_join_encode(peer,check);if (r!=BLOCK_OK) { return r; }
    if (!valid_commit(commit)) { return BLOCK_ARGUMENT; }
    *s=(struct brcm_sae_probe){.attempted=true,.packet_id=packet_id,.active_packet_id=packet_id,.error=BLOCK_NOT_READY,.confirm_mode=session!=NULL,.session=session,.handoff=handoff};
    for (unsigned i=0;i<98;++i) { s->own_commit[i]=commit[i]; }
    struct adapter a={io,s};const struct brcm_join_probe_io wrapped={{&a,iovar},station,radio,poll,delay,milliseconds,poison,live,transmit,io->extra_events};
    r=brcm_join_probe_run(&wrapped,peer,true,&s->join);
    if (r==BLOCK_OK) {
        if (!s->join.down || !s->join.policy.restored) { r=BLOCK_NOT_READY; }
        else if (!s->confirm_mode) {
            s->phase=5;s->reflection=crypto_equal(s->own_commit+2,s->peer_commit+2,96);
            s->peer_valid=!s->reflection && valid_commit(s->peer_commit);if (!s->peer_valid) { r=BLOCK_RANGE; }
        }
    }
    sae_session_clear(session);s->session=NULL;s->handoff=NULL;
    crypto_wipe(s->own_commit,sizeof(s->own_commit));crypto_wipe(s->peer_commit,sizeof(s->peer_commit));
    crypto_wipe(s->own_confirm,sizeof(s->own_confirm));crypto_wipe(s->peer_confirm,sizeof(s->peer_confirm));s->erased=true;
    if (r==BLOCK_OK) { s->phase=s->handoff_attempted ? 12 : s->confirm_mode ? 10 : 6; }s->error=r;return r;
}
enum block_result brcm_sae_probe_run(const struct brcm_join_probe_io *io,const struct brcm_auth_peer *peer,const unsigned char commit[98],uint32_t id,bool prerequisites,struct brcm_sae_probe *s)
{ return run(io,peer,commit,NULL,id,prerequisites,NULL,s); }
enum block_result brcm_sae_auth_run(const struct brcm_join_probe_io *io,const struct brcm_auth_peer *peer,struct sae_session *session,uint32_t id,bool prerequisites,struct brcm_sae_probe *s)
{
    if (!session || session->phase!=SAE_COMMITTED) { return BLOCK_ARGUMENT; }
    unsigned char commit[98];if (sae_session_commit(session,commit)!=SAE_OK) { return BLOCK_ARGUMENT; }
    enum block_result r=run(io,peer,commit,session,id,prerequisites,NULL,s);crypto_wipe(commit,sizeof(commit));return r;
}
enum block_result brcm_sae_handoff_run(const struct brcm_join_probe_io *io,const struct brcm_auth_peer *peer,struct sae_session *session,uint32_t id,bool prerequisites,const struct brcm_sae_handoff *handoff,struct brcm_sae_probe *s)
{
    if (!session || session->phase!=SAE_COMMITTED || !handoff || !handoff->verified) { return BLOCK_ARGUMENT; }
    unsigned char commit[98];if (sae_session_commit(session,commit)!=SAE_OK) { return BLOCK_ARGUMENT; }
    enum block_result r=run(io,peer,commit,session,id,prerequisites,handoff,s);crypto_wipe(commit,sizeof(commit));return r;
}
