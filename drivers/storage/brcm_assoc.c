/* Original narrow association handoff. Wire definitions: brcmfmac
 * cfg80211.c/fwil_types.h and hostap common EAPOL-Key definitions. */
#include "brcm_assoc.h"
#include "crypto.h"
static uint16_t be16(const unsigned char *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool zero(const unsigned char *p,size_t n) { unsigned any=0;for (size_t i=0;i<n;++i) { any|=p[i]; }return !any; }
/* RSN v1: CCMP-128 group/pairwise, SAE AKM-8 only, MFPC+MFPR, no initial
 * PMKID, BIP-CMAC-128 management cipher. Firmware receives the PMKID later. */
static const unsigned char rsn[]={0x30,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,0xc0,0,0,0,0,15,172,6};
enum block_result brcm_assoc_rsn(void *out,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }if (!out || !actual) { return BLOCK_ARGUMENT; }if (capacity<sizeof(rsn)) { return BLOCK_RANGE; }
    for (unsigned i=0;i<sizeof(rsn);++i) { ((unsigned char *)out)[i]=rsn[i]; }*actual=sizeof(rsn);return BLOCK_OK;
}
enum block_result brcm_assoc_auth_status(const struct brcm_auth_peer *peer,const unsigned char pmkid[16],unsigned char out[60])
{
    if (!out || !pmkid) { return BLOCK_ARGUMENT; }unsigned char check[70];enum block_result r=brcm_join_encode(peer,check);if (r!=BLOCK_OK) { return r; }
    unsigned char p[60]={4,0};for (unsigned i=0;i<6;++i) { p[2+i]=peer->peer[i]; }put32(p+8,peer->ssid_length);
    for (unsigned i=0;i<peer->ssid_length;++i) { p[12+i]=peer->ssid[i]; }for (unsigned i=0;i<16;++i) { p[44+i]=pmkid[i]; }
    for (unsigned i=0;i<60;++i) { out[i]=p[i]; }crypto_wipe(p,sizeof(p));return BLOCK_OK;
}
enum block_result brcm_assoc_request_check(const struct brcm_auth_peer *peer,const unsigned char pmkid[16],const void *buffer,size_t n,bool *h2e,struct brcm_assoc_probe *diag)
{
    if (!peer || !pmkid || !buffer || !h2e || !peer->ssid_length || peer->ssid_length>32) { return BLOCK_ARGUMENT; }
    *h2e=false;if (diag) { diag->request_error=0;diag->request_error_offset=0;diag->request_rsn_error=RSN_OK;diag->request_tag=0; }
    if (!n || n>512) { if (diag) { diag->request_error=1; }return BLOCK_RANGE; }const unsigned char *p=buffer;bool ssid=false,security=false,extension=false;
    for (size_t at=0;at<n;) {
        if (diag) { diag->request_error_offset=at;diag->request_tag=p[at]; }
        if (n-at<2) { if (diag) { diag->request_error=1; }return BLOCK_RANGE; }unsigned tag=p[at],bytes=p[at+1];size_t len=2u+bytes;
        if (len>n-at) { if (diag) { diag->request_error=1; }return BLOCK_RANGE; }const unsigned char *ie=p+at;
        if (!tag) {
            if (ssid || bytes!=peer->ssid_length || !crypto_equal(ie+2,peer->ssid,bytes)) { if (diag) { diag->request_error=2; }return BLOCK_UNSUPPORTED; }ssid=true;
        }else if (tag==48) {
            if (security) { if (diag) { diag->request_error=3; }return BLOCK_UNSUPPORTED; }
            struct wpa_rsn_info info;enum wpa_rsn_result checked=wpa_sae_rsn(ie,len,pmkid,&info);
            if (diag) { diag->request_rsn_offset=at;diag->request_rsn_bytes=(unsigned)len;diag->request_rsn=info;diag->request_rsn_error=checked; }
            if (checked!=RSN_OK) { if (diag) { diag->request_error=4; }return BLOCK_UNSUPPORTED; }security=true;
        }else if (tag==244) {
            if (extension || !bytes || bytes>16 || bytes!=(unsigned)(ie[2]&15)+1 || (!!(ie[2]&32)==peer->hunting)) { if (diag) { diag->request_error=5; }return BLOCK_UNSUPPORTED; }
            extension=true;*h2e=!!(ie[2]&32);
        }else if (tag==221 && bytes>=4 && ie[2]==0 && ie[3]==0x50 && ie[4]==0xf2 && ie[5]==1) { if (diag) { diag->request_error=6; }return BLOCK_UNSUPPORTED; }
        at+=len;
    }
    if (diag) { diag->request_error=ssid ? security ? 0 : 8 : 7; }
    return ssid && security ? BLOCK_OK : BLOCK_UNSUPPORTED;
}
enum block_result brcm_assoc_request_ies(const struct brcm_auth_peer *peer,const unsigned char pmkid[16],const void *buffer,size_t n,bool *h2e)
{ return brcm_assoc_request_check(peer,pmkid,buffer,n,h2e,NULL); }
static void event_fail(struct brcm_assoc_probe *s,const struct brcm_auth_message *m)
{
    if (s->event_error!=BLOCK_OK) { return; }s->event_error=BLOCK_IO;
    s->failure_event=m->wire_event;s->failure_status=m->wire_status;s->failure_reason=m->wire_reason;
}
void brcm_assoc_event(void *ctx,const void *payload,size_t bytes)
{
    struct brcm_assoc_probe *s=ctx;if (!s) { return; }brcm_sae_probe_event(&s->sae,payload,bytes);
    if (!s->armed || !s->status_issued || !s->sae.confirmed || !s->sae.handoff_attempted || s->event_error!=BLOCK_OK) { return; }
    if (s->sae.event_error!=BLOCK_OK) { s->event_error=s->sae.event_error;return; }
    if (++s->events>256) { s->event_error=BLOCK_RANGE;return; }
    struct brcm_auth_message m;enum block_result decoded=brcm_auth_event(&s->peer,0,payload,bytes,&m);
    if ((decoded!=BLOCK_OK && decoded!=BLOCK_NOT_READY) || !m.envelope_valid || m.wire_interface || m.wire_bss || !crypto_equal(m.wire_peer,s->peer.peer,6)) { ++s->ignored;return; }
    s->last_event=m.wire_event;s->last_status=m.wire_status;s->last_reason=m.wire_reason;s->last_flags=m.wire_flags;
    switch (m.wire_event) {
    case 0: if (m.wire_status) { event_fail(s,&m); }else { s->ssid_event=true; }break;
    case 3: case 7: case 9:
        if (m.wire_status) { event_fail(s,&m); }else if (m.wire_event!=3) { s->assoc_event=true; }break;
    case 16:
        if (m.wire_status || !(m.wire_flags&1)) { event_fail(s,&m); }else { s->link_event=true; }break;
    case 5: case 6: case 11: case 12: event_fail(s,&m);break;
    default: ++s->ignored;break;
    }
}
void brcm_assoc_data(void *ctx,const void *payload,size_t n)
{
    struct brcm_assoc_probe *s=ctx;if (!s || !s->armed || !s->status_issued || !s->sae.confirmed || !s->sae.handoff_attempted || s->event_error!=BLOCK_OK) { return; }
    if (++s->data_frames>256) { s->event_error=BLOCK_RANGE;return; }
    const unsigned char *p=payload;
    if (!p || n<4 || (p[0]>>4)!=2) { ++s->data_bad;return; }
    if (p[2]&15) { ++s->data_ignored;return; }
    size_t offset=4u+(size_t)p[3]*4;if (offset>n || n-offset<14) { ++s->data_bad;return; }
    p+=offset;n-=offset;
    if (s->dhcp_probe && s->m4_written && s->keys.installed && be16(p+12)!=0x888e) {
        if (s->network.armed) { netprobe_receive(&s->network,p,n);return; }
        if (s->dhcp.armed) { dhcp_receive(&s->dhcp,p,n);return; }
    }
    if (!crypto_equal(p,s->peer.local,6) || !crypto_equal(p+6,s->peer.peer,6) || be16(p+12)!=0x888e) { ++s->data_ignored;return; }
    p+=14;n-=14;
    if (n<4 || p[0]<1 || p[0]>3 || p[1]!=3) { ++s->data_bad;s->event_error=BLOCK_RANGE;return; }
    size_t body=be16(p+2);s->eapol_version=p[0];
    if (body<95 || body>2048 || body>n-4) { ++s->data_bad;s->event_error=BLOCK_RANGE;return; }
    if (s->key_probe) {
        s->descriptor=p[4];s->key_info=be16(p+5);
        s->key_result=wpa_probe_receive(s->private_exchange,p,n);
        if (s->key_result!=WPA_PROBE_OK) { ++s->data_bad;s->event_error=BLOCK_UNSUPPORTED;return; }
        s->m1_seen=s->private_exchange->m1;s->m3_bytes=(unsigned)s->private_exchange->m3_bytes;return;
    }
    p+=4;s->descriptor=p[0];s->key_info=be16(p+1);
    /* SAE AKM-defined descriptor version 0, pairwise + ACK only. M1 has no
     * MIC and is not authenticated; observation must never authorize a port. */
    if (p[0]!=2 || s->key_info!=0x88 || be16(p+3)!=16 || zero(p+13,32) || !zero(p+77,16) || be16(p+93)!=body-95) {
        ++s->data_bad;s->event_error=BLOCK_UNSUPPORTED;return;
    }
    s->m1_seen=true;
}
struct adapter { const struct brcm_assoc_io *base;struct brcm_assoc_probe *s;struct wpa_probe exchange; };
static enum block_result iovar(void *ctx,bool set,const char *name,const void *in,size_t n,void *out,size_t cap,size_t *actual,uint32_t *status)
{ struct adapter *a=ctx;return a->base->join.policy.iovar(a->base->join.policy.context,set,name,in,n,out,cap,actual,status); }
static enum block_result get_ie(struct adapter *a,unsigned char out[256],size_t *bytes)
{
    size_t n=0;enum block_result r=iovar(a,false,"wpaie",NULL,0,out,256,&n,&a->s->ie_status);if (r!=BLOCK_OK) { return r; }
    if (n>256) { return BLOCK_RANGE; }size_t at=0;bool rsn_seen=false;
    while (at<n) {
        if (!out[at]) { if (!zero(out+at,n-at)) { return BLOCK_UNSUPPORTED; }break; }
        if (n-at<2 || (out[at]!=0x30 && out[at]!=244)) { return BLOCK_UNSUPPORTED; }
        size_t len=2u+out[at+1];if (len>n-at) { return BLOCK_RANGE; }
        if (out[at]==0x30) {
            if (rsn_seen || len<4 || out[at+2]!=1 || out[at+3]) { return BLOCK_UNSUPPORTED; }rsn_seen=true;
        }
        at+=len;
    }
    if (at && !rsn_seen) { return BLOCK_UNSUPPORTED; }*bytes=at;return BLOCK_OK;
}
static enum block_result prepare_ie(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;s->phase=1;enum block_result r;
    if (s->ie_owned) {
        /* This kernel has never set a station IE before this one-shot call.
         * wpaie GET can be synthesized, not a serialization of the app-owned
         * override. Keep it diagnostic; establish an explicit empty baseline
         * by acknowledged SET, then validate emitted request IEs after join. */
        unsigned char reply[256]={0};size_t n=0;
        r=iovar(a,false,"wpaie",NULL,0,reply,sizeof(reply),&n,&s->ie_query_status);
        s->ie_query_seen=true;s->ie_query_result=r;s->ie_query_bytes=n;
        for (size_t i=0;i<n && i<sizeof(s->ie_query_prefix);++i) { s->ie_query_prefix[i]=reply[i]; }
        if (r!=BLOCK_OK && !(r==BLOCK_UNSUPPORTED && s->ie_query_status)) { s->ie_error=r;return r; }
        s->ie_changed=true;r=iovar(a,true,"wpaie",NULL,0,NULL,0,&n,&s->ie_status);
        if (r!=BLOCK_OK) { s->ie_error=r;return r; }s->ie_baseline_ack=true;
        r=iovar(a,true,"wpaie",rsn,sizeof(rsn),NULL,0,&n,&s->ie_status);
        s->ie_error=r;s->ie_set_ack=r==BLOCK_OK;return r;
    }
    r=get_ie(a,s->saved_ie,&s->saved_ie_bytes);
    if (r!=BLOCK_OK) { s->ie_error=r;return r; }s->ie_snapshot=true;s->ie_changed=true;size_t n=0;
    r=iovar(a,true,"wpaie",rsn,sizeof(rsn),NULL,0,&n,&s->ie_status);if (r!=BLOCK_OK) { s->ie_error=r;return r; }s->ie_set_ack=true;
    unsigned char current[256];r=get_ie(a,current,&n);
    if (r==BLOCK_OK && (n!=sizeof(rsn) || !crypto_equal(current,rsn,n))) { r=BLOCK_IO; }
    s->ie_error=r;s->ie_verified=r==BLOCK_OK;return r;
}
static enum block_result restore_ie(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;if (!s->ie_changed) { return BLOCK_OK; }size_t n=0;
    enum block_result r=iovar(a,true,"wpaie",s->saved_ie_bytes ? s->saved_ie : NULL,s->saved_ie_bytes,NULL,0,&n,&s->ie_status);
    if (s->ie_owned) { s->ie_clear_ack=r==BLOCK_OK; }
    else if (r==BLOCK_OK) { unsigned char current[256];r=get_ie(a,current,&n);if (r==BLOCK_OK && (n!=s->saved_ie_bytes || !crypto_equal(current,s->saved_ie,n))) { r=BLOCK_IO; } }
    s->restore_error=r;s->ie_restored=r==BLOCK_OK;return r;
}
static enum block_result station(void *ctx,uint32_t *status)
{
    struct adapter *a=ctx;enum block_result r=a->base->join.station(a->base->join.policy.context,status);
    /* Configure the application IE before WPA/security selection, matching
     * the reference connection order. The station check established DOWN. */
    return r==BLOCK_OK && a->s->ie_owned ? prepare_ie(a) : r;
}
static enum block_result live(void *ctx,uint32_t *status)
{ struct adapter *a=ctx;return a->base->join.station_live(a->base->join.policy.context,status); }
static bool key_permitted(void *ctx)
{
    struct adapter *a=ctx;const struct brcm_assoc_probe *s=a->s;
    return s->armed && s->status_ack && s->association_verified && s->m2_written && s->m3_mic_verified && s->key_data_verified && s->event_error==BLOCK_OK && s->sae.event_error==BLOCK_OK && a->exchange.group_retained;
}
static enum block_result key_set(void *ctx,const void *wire,size_t n,uint32_t *status)
{ struct adapter *a=ctx;return a->base->key_set(a->base->join.policy.context,wire,n,status); }
static struct brcm_key_io key_io(struct adapter *a) { return (struct brcm_key_io){a,key_set,key_permitted}; }
static enum block_result radio(void *ctx,bool up,uint32_t *status)
{
    struct adapter *a=ctx;enum block_result r;
    if (up && !a->s->ie_owned) { r=prepare_ie(a);if (r!=BLOCK_OK) { return r; } }
    if (!up && a->s->port_issued && !a->s->port_closed) {
        a->s->port_close_active=true;
        a->s->port_close_result=a->base->port_set(a->base->join.policy.context,a->s->peer.peer,false,&a->s->port_close_status);
        a->s->port_closed=a->s->port_close_result==BLOCK_OK;a->s->port_close_active=false;
    }
    r=a->base->join.radio(a->base->join.policy.context,up,status);
    if (!up && a->s->install_probe) { struct brcm_key_io kio=key_io(a);(void)brcm_keys_clear(&kio,&a->s->keys); }
    if (!up) { if (r==BLOCK_OK) { (void)restore_ie(a); }else if (a->s->ie_changed) { a->s->restore_error=BLOCK_NOT_READY; } }
    else if (r==BLOCK_OK && !a->s->ie_owned) {
        unsigned char current[256];size_t n=0;r=get_ie(a,current,&n);
        if (r==BLOCK_OK && (n!=sizeof(rsn) || !crypto_equal(current,rsn,n))) { r=BLOCK_IO; }
        a->s->ie_error=r;a->s->ie_verified=r==BLOCK_OK;
    }return r;
}
static enum block_result poll(void *ctx)
{ struct adapter *a=ctx;return a->base->join.poll(a->base->join.policy.context); }
static enum block_result delay(void *ctx,unsigned us)
{ struct adapter *a=ctx;return a->base->join.delay_us(a->base->join.policy.context,us); }
static uint64_t milliseconds(void *ctx)
{ struct adapter *a=ctx;return a->base->join.milliseconds(a->base->join.policy.context); }
static void poison(void *ctx,enum block_result r)
{ struct adapter *a=ctx;a->base->join.poison(a->base->join.policy.context,r); }
static enum block_result request_ies(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;unsigned char reply[512]={0};size_t n=0;
    s->assoc_query_phase=1;enum block_result r=iovar(a,false,"assoc_info",NULL,0,reply,sizeof(reply),&n,&s->identity_status);
    s->assoc_info_reply_bytes=n;for (unsigned i=0;i<3 && 4u*i+4<=n;++i) { s->assoc_info_words[i]=le32(reply+4*i); }
    if (r!=BLOCK_OK) { return r; }if (n<8) { return BLOCK_RANGE; }
    size_t bytes=le32(reply);if (!bytes) { return BLOCK_NOT_READY; }
    /* This firmware reports req_len INCLUDING the fixed association-request
     * header; assoc_req_ies excludes it. Normalize at this ABI boundary only.
     * Reassociation adds another six bytes and is outside this fresh probe. */
    if (s->request_fixed_bytes) {
        if (n<12 || s->assoc_info_words[2]) { return BLOCK_UNSUPPORTED; }
        if (bytes<s->request_fixed_bytes) { return BLOCK_RANGE; }bytes-=s->request_fixed_bytes;
    }
    s->assoc_ie_bytes=bytes;
    if (!bytes || bytes>sizeof(reply) || le32(reply+4)>sizeof(reply)) { return BLOCK_RANGE; }
    s->assoc_query_phase=2;r=iovar(a,false,"assoc_req_ies",NULL,0,reply,sizeof(reply),&n,&s->identity_status);s->assoc_req_reply_bytes=n;
    if (r!=BLOCK_OK) { return r; }if (n<bytes) { return BLOCK_RANGE; }
    s->request_saved=bytes;for (size_t i=0;i<bytes;++i) { s->request_ies[i]=reply[i]; }
    s->assoc_query_phase=3;r=brcm_assoc_request_check(&s->peer,s->pmkid,reply,bytes,&s->request_h2e,s);
    s->assoc_ie_verified=r==BLOCK_OK;if (s->ie_owned) { s->ie_verified=s->assoc_ie_verified; }return r;
}
static enum block_result identity(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;struct brcm_link_identity value;uint32_t status=0;++s->identity_queries;
    enum block_result r=a->base->identity(a->base->join.policy.context,&value,&status);s->identity_status=status;if (r!=BLOCK_OK) { return r; }
    if (zero(value.bssid,6)) { return BLOCK_NOT_READY; }
    if (!crypto_equal(value.bssid,s->peer.peer,6) || value.ssid_length!=s->peer.ssid_length || !crypto_equal(value.ssid,s->peer.ssid,value.ssid_length)) { return BLOCK_IO; }
    s->identity_verified=true;static const char *const names[]={"wpa_auth","wsec","mfp"};static const uint32_t expected[]={0x40000,4,2};
    for (unsigned i=0;i<3;++i) {
        unsigned char p[4];size_t n=0;r=iovar(a,false,names[i],NULL,0,p,4,&n,&s->identity_status);if (r!=BLOCK_OK) { return r; }if (n!=4) { return BLOCK_RANGE; }
        uint32_t v=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        if (v!=expected[i] && !(i==1 && v==0x604)) { return BLOCK_UNSUPPORTED; }
    }
    s->policy_verified=true;
    if (s->ie_owned) { r=request_ies(a);if (r!=BLOCK_OK) { return r; } }
    if (s->event_error!=BLOCK_OK) { return s->event_error; }
    if (s->sae.event_error!=BLOCK_OK) { return s->sae.event_error; }
    s->association_verified=true;return BLOCK_OK;
}
static void retry_status(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;const struct wpa_probe *w=&a->exchange;
    s->m3_duplicates=w->m3_duplicates;s->m3_retries_queued=w->retries_queued;s->m3_retries_verified=w->retries_verified;s->m3_retry_pending=w->retry_pending;
    s->m3_retry_rejection=w->retry_rejection;
}
static enum block_result refresh_m3(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;if (s->event_error!=BLOCK_OK) { return s->event_error; }
    s->key_result=wpa_probe_verify(&a->exchange);retry_status(a);
    if (s->key_result!=WPA_PROBE_OK) { return BLOCK_UNSUPPORTED; }return BLOCK_OK;
}
static enum block_result transmit_m4(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;struct wpa_probe *w=&a->exchange;
    /* One initial M4 plus at most three retry responses. No key SET occurs
     * here. A retry during credit pumping invalidates stale response bytes. */
    for (unsigned attempt=0;attempt<8;++attempt) {
        enum block_result r=refresh_m3(a);if (r!=BLOCK_OK) { return r; }
        if (!w->m4_due && s->m4_written) { return BLOCK_OK; }
        if (s->m4_writes>=4) { s->key_result=WPA_PROBE_LIMIT;return BLOCK_RANGE; }
        unsigned char frame[113]={0};size_t bytes=0;
        s->key_result=wpa_probe_m4(w,s->peer.local,s->peer.peer,frame,sizeof(frame),&bytes);
        s->m4_encoded=w->m4_encoded;s->m4_bytes=(unsigned)bytes;
        if (s->key_result!=WPA_PROBE_OK) { crypto_wipe(frame,sizeof(frame));return BLOCK_UNSUPPORTED; }
        s->m4_attempted=true;r=a->base->m4_tx(a->base->join.policy.context,frame,bytes);crypto_wipe(frame,sizeof(frame));retry_status(a);
        if (r==BLOCK_NOT_READY && !w->failed && s->event_error==BLOCK_OK && (w->retry_pending || w->m4_due)) { ++s->m4_deferred;continue; }
        if (r!=BLOCK_OK) { return r; }s->m4_written=true;++s->m4_writes;s->key_phase=6;
        if (!w->retry_pending && !w->m4_due) { return BLOCK_OK; }
    }
    s->key_result=WPA_PROBE_LIMIT;return BLOCK_RANGE;
}
static bool network_handshake_ready(void *ctx)
{
    struct adapter *a=ctx;const struct brcm_assoc_probe *s=a->s;
    return key_permitted(a) && s->keys.installed && s->m4_written && s->post_m4_identity && !a->exchange.retry_pending && !a->exchange.m4_due && !a->exchange.failed;
}
static bool network_ready(void *ctx)
{ struct adapter *a=ctx;return network_handshake_ready(a) && a->s->port_verified; }
static enum block_result network_poll(void *ctx)
{
    struct adapter *a=ctx;enum block_result r=poll(a);if (r!=BLOCK_OK) { return r; }if (a->s->event_error!=BLOCK_OK) { return a->s->event_error; }
    if (a->exchange.retry_pending || a->exchange.m4_due) { return transmit_m4(a); }return BLOCK_OK;
}
static enum block_result network_tx(void *ctx,const void *frame,size_t n)
{ struct adapter *a=ctx;return a->base->dhcp_tx(a->base->join.policy.context,frame,n); }
static enum block_result peer_info(struct adapter *a,struct brcm_station_info *out,uint32_t *status)
{
    ++a->s->port_queries;enum block_result r=a->base->peer_info(a->base->join.policy.context,a->s->peer.peer,out,status);
    if (r==BLOCK_OK && (!out->valid || !crypto_equal(out->peer,a->s->peer.peer,6) || (out->flags&0x18)!=0x18)) { return BLOCK_NOT_READY; }return r;
}
static enum block_result port_open(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;s->port_attempted=true;s->port_phase=1;
    enum block_result r=peer_info(a,&s->port_before,&s->port_status);
    if (r!=BLOCK_OK) { s->port_result=r;return r; }
    s->port_phase=2;
    for (unsigned attempt=0;attempt<8;++attempt) {
        r=network_poll(a);if (r!=BLOCK_OK || !network_handshake_ready(a)) { if (r==BLOCK_OK) { r=BLOCK_NOT_READY; }break; }
        s->port_issued=true;r=a->base->port_set(a->base->join.policy.context,s->peer.peer,true,&s->port_status);
        if (r==BLOCK_NOT_READY && (a->exchange.retry_pending || a->exchange.m4_due)) { ++s->port_deferrals;continue; }
        break;
    }
    if (r==BLOCK_OK) {
        s->port_ack=true;s->port_phase=3;r=peer_info(a,&s->port_after,&s->port_status);
        if (r==BLOCK_OK && !(s->port_after.flags&0x20)) { r=BLOCK_NOT_READY; }
        if (r==BLOCK_OK) { s->port_verified=true;s->port_phase=4; }
    }
    s->port_result=r;return r;
}
static enum block_result network_probe(struct adapter *a)
{
    unsigned char entropy[32]={0};bool random=a->base->random(a->base->join.policy.context,entropy);enum block_result r=BLOCK_IO;
    if (random) {
        const struct dhcp_io io={a,network_tx,network_poll,delay,milliseconds,network_ready};
        a->s->lease_origin=milliseconds(a);r=dhcp_probe_run(&io,a->s->peer.local,entropy,&a->s->dhcp);
    }
    crypto_wipe(entropy,sizeof(entropy));return r;
}
static enum block_result gateway_tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *frame,size_t n)
{ struct adapter *a=ctx;return a->base->net_tx(a->base->join.policy.context,kind,sequence,frame,n); }
static void decline_lease(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;unsigned char frame[DHCP_FRAME_BYTES];size_t bytes=0;s->decline_attempted=true;s->decline_active=true;
    enum block_result r=dhcp_encode_decline(&s->dhcp,frame,sizeof(frame),&bytes);
    for (unsigned attempt=0;r==BLOCK_OK && attempt<8;++attempt) {
        r=network_poll(a);if (r!=BLOCK_OK) { break; }if (!network_ready(a)) { r=BLOCK_NOT_READY;break; }
        r=network_tx(a,frame,bytes);if (r==BLOCK_OK) { s->decline_written=true;break; }
        if (r==BLOCK_NOT_READY && (a->exchange.retry_pending || a->exchange.m4_due) && attempt<7) { r=BLOCK_OK;continue; }break;
    }
    s->decline_result=r;s->decline_active=false;
}
static enum block_result gateway_probe(struct adapter *a)
{
    struct brcm_assoc_probe *s=a->s;unsigned char entropy[32]={0};s->gateway_attempted=true;enum block_result r=BLOCK_IO;
    if (a->base->random(a->base->join.policy.context,entropy)) {
        const struct netprobe_io io={a,gateway_tx,network_poll,delay,milliseconds,network_ready};
        r=netprobe_run(&io,s->peer.local,&s->dhcp.lease,entropy,s->lease_origin,&s->network);
    }
    crypto_wipe(entropy,sizeof(entropy));s->gateway_result=r;if (s->network.conflict) { decline_lease(a); }return r;
}
static enum block_result verified(void *ctx,struct sae_session *session,const struct brcm_auth_peer *peer)
{
    struct adapter *a=ctx;struct brcm_assoc_probe *s=a->s;
    if (!s->sae.confirmed || !s->sae.handoff_attempted || session->phase!=SAE_VERIFIED || !(s->ie_owned ? s->ie_baseline_ack && s->ie_set_ack : s->ie_verified) || !s->sae.join.live_mask_verified || !crypto_equal(peer->peer,s->peer.peer,6)) { return BLOCK_NOT_READY; }
    struct sae_keys keys={0};s->phase=3;enum sae_result sr=sae_session_take_keys(session,&keys);
    if (sr!=SAE_OK) { crypto_wipe(&keys,sizeof(keys));return BLOCK_NOT_READY; }
    for (unsigned i=0;i<16;++i) { s->pmkid[i]=keys.pmkid[i]; }
    if (s->key_probe) {
        unsigned char nonce[32]={0};s->key_phase=1;
        bool fresh=a->base->random(a->base->join.policy.context,nonce);
        s->key_result=fresh ? wpa_probe_seed(&a->exchange,keys.pmk,nonce) : WPA_PROBE_NONCE;
        crypto_wipe(nonce,sizeof(nonce));
        if (s->key_result==WPA_PROBE_OK && s->key_data_probe) {
            s->key_result=wpa_probe_bind_ap(&a->exchange,a->base->ap_rsn,a->base->ap_rsn_bytes);
            s->key_data_result=a->exchange.key_data_result;
            a->exchange.retain_group=s->install_probe;
        }
        if (s->key_result!=WPA_PROBE_OK) { crypto_wipe(&keys,sizeof(keys));s->keys_erased=true;return BLOCK_IO; }
    }
    unsigned char params[60];enum block_result r=brcm_assoc_auth_status(peer,keys.pmkid,params);
    crypto_wipe(&keys,sizeof(keys));s->keys_erased=!s->key_probe;if (r!=BLOCK_OK) { return r; }
    s->phase=4;s->armed=true;s->status_issued=true;size_t n=0;
    r=iovar(a,true,"auth_status",params,sizeof(params),NULL,0,&n,&s->auth_status);crypto_wipe(params,sizeof(params));
    if (r!=BLOCK_OK) { s->armed=false;return r; }s->status_ack=true;s->phase=5;uint64_t start=milliseconds(a);
    for (unsigned i=0;i<1000;++i) {
        if (s->event_error!=BLOCK_OK) { r=s->event_error;break; }
        if ((s->ssid_event || s->link_event) && !s->association_verified && i%10==0) {
            r=identity(a);if (r!=BLOCK_OK && r!=BLOCK_NOT_READY) { break; }
        }
        if (s->event_error!=BLOCK_OK) { r=s->event_error;break; }
        if (s->association_verified && s->m1_seen) {
            if (!s->key_probe) { r=BLOCK_OK;s->phase=6;break; }
            if (!s->m2_attempted) {
                unsigned char frame[370]={0};size_t bytes=0;s->key_phase=2;
                /* H2E M2 needs RSNXE binding, not supported by this initial
                 * RSN-only probe. Do not silently omit that negotiation. */
                if (!s->peer.hunting || s->request_h2e) { s->key_result=WPA_PROBE_FORMAT;r=BLOCK_UNSUPPORTED;break; }
                s->key_result=wpa_probe_m2(&a->exchange,s->peer.local,s->peer.peer,s->request_ies+s->request_rsn_offset,s->request_rsn_bytes,s->pmkid,frame,sizeof(frame),&bytes);
                if (s->key_result!=WPA_PROBE_OK) { crypto_wipe(frame,sizeof(frame));r=BLOCK_UNSUPPORTED;break; }
                s->m2_attempted=true;s->m2_bytes=(unsigned)bytes;
                r=a->base->data_tx(a->base->join.policy.context,frame,bytes);crypto_wipe(frame,sizeof(frame));
                if (r!=BLOCK_OK) { break; }s->m2_written=true;a->exchange.m2_written=true;s->key_phase=3;
                start=milliseconds(a); /* A fresh bounded M3 wait, after M2. */
            }
            if (s->event_error!=BLOCK_OK) { r=s->event_error;break; }
            s->key_result=wpa_probe_verify(&a->exchange);
            s->m3_mic_verified=a->exchange.mic_verified;s->m3_unwrap_verified=a->exchange.unwrap_verified;s->unwrapped_bytes=(unsigned)a->exchange.plain_bytes;
            if (s->key_data_probe) { s->key_data=a->exchange.key_data;s->key_data_result=a->exchange.key_data_result;s->key_data_verified=a->exchange.key_data_verified; }
            if (s->key_result==WPA_PROBE_OK && s->key_data_probe) {
                unsigned char frame[113]={0};size_t bytes=0;s->key_phase=4;
                if (s->install_probe) {
                    struct brcm_key_io kio=key_io(a);s->key_phase=5;
                    /* Initial M4 must leave the host BEFORE enabling the TX TK,
                     * as in the ordinary host supplicant path. SDIO completion
                     * is our bus-ordering fence; it is not an air ACK. */
                    r=identity(a);if (r==BLOCK_OK) { r=transmit_m4(a); }if (r!=BLOCK_OK) { break; }
                    s->first_m4_before_keys=s->m4_written && !s->keys.attempted;
                    r=brcm_keys_install(&kio,s->peer.peer,a->exchange.ptk.tk,&a->exchange.group,&s->keys);
                    if (r==BLOCK_OK && s->event_error!=BLOCK_OK) { r=s->event_error; }
                    if (r==BLOCK_OK) { r=identity(a); } /* Keys must not weaken SAE/AES/PMF. */
                    if (r==BLOCK_OK) { r=transmit_m4(a); }if (r!=BLOCK_OK) { break; }
                    for (unsigned p=0;p<20;++p) {
                        ++s->settle_polls;r=poll(a);if (r!=BLOCK_OK) { break; }if (s->event_error!=BLOCK_OK) { r=s->event_error;break; }
                        if (a->exchange.retry_pending || a->exchange.m4_due) { r=transmit_m4(a);if (r!=BLOCK_OK) { break; } }
                        r=delay(a,10000);if (r!=BLOCK_OK) { break; }
                    }
                    if (r==BLOCK_OK) { r=identity(a);s->post_m4_identity=r==BLOCK_OK; }
                    if (r==BLOCK_OK && (a->exchange.retry_pending || a->exchange.m4_due)) { r=transmit_m4(a); }
                    if (r==BLOCK_OK && s->dhcp_probe) {
                        s->key_phase=8;r=port_open(a);
                        if (r==BLOCK_OK) {
                            r=network_probe(a);
                            if (r==BLOCK_OK && s->gateway_probe) { s->key_phase=10;r=gateway_probe(a); }
                            s->port_final_result=peer_info(a,&s->port_final,&s->port_final_status);
                            if (s->port_final_result==BLOCK_OK && !(s->port_final.flags&0x20)) { s->port_final_result=BLOCK_NOT_READY; }
                            if (r==BLOCK_OK) { r=s->port_final_result; }
                        }
                        if (r==BLOCK_OK) { r=identity(a); }if (r==BLOCK_OK) { r=network_poll(a); }
                    }
                    if (r==BLOCK_OK) { s->phase=s->gateway_probe ? 11 : s->dhcp_probe ? 10 : 9;s->key_phase=s->gateway_probe ? 11 : s->dhcp_probe ? 9 : 7; }break;
                }
                s->key_result=wpa_probe_m4(&a->exchange,s->peer.local,s->peer.peer,frame,sizeof(frame),&bytes);
                s->m4_encoded=a->exchange.m4_encoded;s->m4_bytes=(unsigned)bytes;
                crypto_wipe(frame,sizeof(frame));if (s->key_result!=WPA_PROBE_OK) { r=BLOCK_UNSUPPORTED;break; }
                s->key_phase=5;s->phase=8;r=BLOCK_OK;break;
            }
            if (s->key_result==WPA_PROBE_OK) { s->key_phase=4;s->phase=7;r=BLOCK_OK;break; }
            if (s->key_result!=WPA_PROBE_WAIT) { r=BLOCK_UNSUPPORTED;break; }
        }
        r=BLOCK_TIMEOUT;if (milliseconds(a)-start>=10000) { break; }
        ++s->polls;enum block_result step=poll(a);if (step!=BLOCK_OK) { r=step;break; }
        if (s->event_error!=BLOCK_OK) { r=s->event_error;break; }
        step=delay(a,10000);if (step!=BLOCK_OK) { r=step;break; }
    }
    s->armed=false;return r;
}
enum block_result brcm_assoc_run(const struct brcm_assoc_io *io,const struct brcm_auth_peer *peer,struct sae_session *session,bool prerequisites,struct brcm_assoc_probe *s)
{
    if (!io || !io->identity || !io->join.policy.iovar || !io->join.station || !io->join.station_live || !io->join.radio || !io->join.poll || !io->join.delay_us || !io->join.milliseconds || !io->join.poison || io->join.requested || !s || !peer || !session || session->phase!=SAE_COMMITTED) { return BLOCK_ARGUMENT; }
    if ((io->request_fixed_bytes!=0 && io->request_fixed_bytes!=4) || (io->key_probe && (!io->fresh_ie_owner || !io->data_tx || !io->random))) { return BLOCK_ARGUMENT; }
    if (io->key_data_probe && (!io->key_probe || !io->ap_rsn || !wpa_ap_rsn_profile(io->ap_rsn,io->ap_rsn_bytes))) { return BLOCK_ARGUMENT; }
    if (io->install_probe && (!io->key_data_probe || !io->key_set || !io->m4_tx)) { return BLOCK_ARGUMENT; }
    if (io->dhcp_probe && (!io->install_probe || !io->dhcp_tx || !io->peer_info || !io->port_set)) { return BLOCK_ARGUMENT; }
    if (io->gateway_probe && (!io->dhcp_probe || !io->net_tx)) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }if (!prerequisites) { return BLOCK_NOT_READY; }
    unsigned char check[70];enum block_result r=brcm_join_encode(peer,check);if (r!=BLOCK_OK) { return r; }
    *s=(struct brcm_assoc_probe){.attempted=true,.peer=*peer,.error=BLOCK_NOT_READY,.ie_owned=io->fresh_ie_owner,.request_fixed_bytes=io->request_fixed_bytes,.key_probe=io->key_probe,.key_data_probe=io->key_data_probe,.install_probe=io->install_probe,.dhcp_probe=io->dhcp_probe,.gateway_probe=io->gateway_probe};
    struct adapter a={.base=io,.s=s};if (s->key_probe) { s->private_exchange=&a.exchange; }
    /* SET_SSID, AUTH, DEAUTH/IND, ASSOC/REASSOC, DISASSOC/IND and LINK. */
    uint32_t events=(1u<<0)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11)|(1u<<12)|(1u<<16);
    const struct brcm_join_probe_io wrapped={{&a,iovar},station,radio,poll,delay,milliseconds,poison,live,NULL,io->join.extra_events|events};
    const struct brcm_sae_handoff handoff={&a,verified};r=brcm_sae_handoff_run(&wrapped,peer,session,1,true,&handoff,&s->sae);
    /* A setup/policy failure before UP never reaches the join DOWN callback.
     * Close the already established IE journal on that path too. */
    if (s->ie_owned && s->ie_changed && !s->ie_restored && !s->sae.join.up_attempted) {
        enum block_result down=a.base->join.radio(a.base->join.policy.context,false,&s->sae.join.cleanup_status);
        s->sae.join.cleanup_error=down;s->sae.join.down=down==BLOCK_OK;
        if (down==BLOCK_OK) { (void)restore_ie(&a); }else { s->restore_error=down; }
    }
    if (s->ie_changed && !s->ie_restored) { poison(&a,s->restore_error==BLOCK_OK ? BLOCK_IO : s->restore_error);if (r==BLOCK_OK) { r=BLOCK_IO; } }
    if (s->keys.issued_mask && !s->keys.cleared) { poison(&a,s->keys.clear_error==BLOCK_OK ? BLOCK_IO : s->keys.clear_error);if (r==BLOCK_OK) { r=BLOCK_IO; } }
    if (s->port_issued && !s->port_closed) { poison(&a,s->port_close_result==BLOCK_OK ? BLOCK_IO : s->port_close_result);if (r==BLOCK_OK) { r=BLOCK_IO; } }
    retry_status(&a);s->armed=false;s->network.active=s->network.armed=s->decline_active=false;if (s->key_probe && a.exchange.seeded) { s->keys_erased=true; }
    wpa_probe_clear(&a.exchange);s->private_exchange=NULL;s->error=r;return r;
}
