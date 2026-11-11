/* Bounded external-SAE request scope with optional post-request work, followed
 * by interface DOWN and full policy restoration. Default: observation only. */
#include "brcm_join_probe.h"
static void put32(unsigned char *p,uint32_t v)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool mac(const unsigned char *p)
{ unsigned any=0;for (unsigned i=0;i<6;++i) { any|=p[i]; }return any && !(p[0]&1); }
enum block_result brcm_join_encode(const struct brcm_auth_peer *p,unsigned char out[70])
{
    if (!p || !out || !mac(p->local) || !mac(p->peer) || !p->ssid_length || p->ssid_length>32) { return BLOCK_ARGUMENT; }
    unsigned different=0;for (unsigned i=0;i<6;++i) { different|=p->local[i]^p->peer[i]; }
    if (!different || p->band!=2 || !p->channel || p->channel>13) { return BLOCK_UNSUPPORTED; }
    unsigned char data[70]={0};put32(data,p->ssid_length);
    for (unsigned i=0;i<p->ssid_length;++i) { data[4+i]=p->ssid[i]; }
    /* scan_type=0: firmware default; three padding bytes; four default ints. */
    for (unsigned at=40;at<56;at+=4) { put32(data+at,UINT32_MAX); }
    for (unsigned i=0;i<6;++i) { data[56+i]=p->peer[i]; }
    put32(data+64,1);unsigned chanspec=0x1000u|p->channel;
    data[68]=(unsigned char)chanspec;data[69]=(unsigned char)(chanspec>>8);
    for (unsigned i=0;i<70;++i) { out[i]=data[i]; }return BLOCK_OK;
}
void brcm_join_event(void *context,const void *payload,size_t bytes)
{
    struct brcm_join_probe *s=context;if (!s || !s->armed || !s->join_issued || s->request_seen || s->event_error!=BLOCK_OK) { return; }
    if (++s->events>256) { s->event_error=BLOCK_RANGE;return; }
    struct brcm_auth_message m;enum block_result r=brcm_auth_event(&s->peer,0,payload,bytes,&m);
    if (r==BLOCK_NOT_READY || r==BLOCK_UNSUPPORTED) { ++s->ignored;return; }
    if (r!=BLOCK_OK) {
        if (r==BLOCK_IO && m.kind==BRCM_AUTH_REQUEST) { s->event_error=r; }else { ++s->malformed; }return;
    }
    if (m.kind!=BRCM_AUTH_REQUEST) { ++s->ignored;return; }
    s->request_flags=m.request_flags;s->request_seen=true;
}
struct action_context { const struct brcm_join_probe_io *io;struct brcm_join_probe *state; };
static enum block_result live_check(const struct brcm_join_probe_io *io,struct brcm_join_probe *s)
{
    enum block_result role=io->station_live(io->policy.context,&s->firmware_status);if (role!=BLOCK_OK) { return role; }
    static const char *const names[]={"auth","wpa_auth","wsec","mfp"};
    static const uint32_t expected[]={3,0x40000,4,2};
    for (unsigned i=0;i<4;++i) {
        unsigned char value[4];size_t n=0;enum block_result r=io->policy.iovar(io->policy.context,false,names[i],NULL,0,value,4,&n,&s->firmware_status);
        if (r!=BLOCK_OK) { return r; }if (n!=4) { return BLOCK_RANGE; }
        uint32_t v=(uint32_t)value[0]|(uint32_t)value[1]<<8|(uint32_t)value[2]<<16|(uint32_t)value[3]<<24;
        s->live_policy[i]=v;s->live_read_mask|=1u<<i;
        if (v!=expected[i] && !(i==0 && v==2) && !(i==2 && v==0x604)) { s->live_mismatch_mask|=1u<<i; }
    }
    if (s->live_mismatch_mask) { return BLOCK_IO; }
    unsigned char request[259]={1,0,255,255},reply[259]={0};size_t n=0;
    enum block_result r=io->policy.iovar(io->policy.context,false,"event_msgs_ext",request,259,reply,259,&n,&s->firmware_status);
    if (r!=BLOCK_OK) { return r; }
    bool header=reply[0]==1 || (reply[0]=='e' && reply[1]=='v' && reply[2]==24 && reply[3]==24);
    if (n<4 || n>259 || !header || reply[2]!=s->policy.mask_bytes || n<4u+reply[2]) { return BLOCK_UNSUPPORTED; }
    for (unsigned i=0;i<s->policy.mask_bytes;++i) {
        unsigned char wanted=s->policy.mask[i];if (i==23) { wanted|=0x78; }
        if (i<4) { wanted|=(unsigned char)(s->policy.extra_events>>(8*i)); }
        if (reply[4+i]!=wanted) { return BLOCK_IO; }
    }
    s->live_mask_verified=true;return BLOCK_OK;
}
static enum block_result observe(void *context)
{
    struct action_context *a=context;const struct brcm_join_probe_io *io=a->io;struct brcm_join_probe *s=a->state;
    void *ctx=io->policy.context;unsigned char join[70];enum block_result r=brcm_join_encode(&s->peer,join);
    if (r!=BLOCK_OK) { return r; }
    s->phase=2;s->up_attempted=true;r=io->radio(ctx,true,&s->firmware_status);if (r!=BLOCK_OK) { goto down; }
    /* Interface UP can normalize settings: verify live controls and events
     * again before issuing the sole target-specific join request. */
    s->phase=3;r=live_check(io,s);if (r!=BLOCK_OK) { goto down; }
    s->phase=4;s->armed=true;s->join_issued=true;size_t n=0;
    r=io->policy.iovar(ctx,true,"join",join,sizeof(join),NULL,0,&n,&s->firmware_status);if (r!=BLOCK_OK) { goto down; }
    s->phase=5;uint64_t start=io->milliseconds(ctx);
    for (unsigned i=0;i<1500 && !s->request_seen && s->event_error==BLOCK_OK;++i) {
        if (io->milliseconds(ctx)-start>=15000) { break; }
        ++s->polls;r=io->poll(ctx);if (r!=BLOCK_OK) { goto down; }
        if (s->request_seen || s->event_error!=BLOCK_OK) { break; }
        r=io->delay_us(ctx,10000);if (r!=BLOCK_OK) { goto down; }
    }
    r=s->event_error!=BLOCK_OK ? s->event_error : s->request_seen ? BLOCK_OK : BLOCK_TIMEOUT;
    if (r==BLOCK_OK && io->requested) { s->phase=8;r=io->requested(ctx); }
down:
    s->armed=false;s->cleanup_error=io->radio(ctx,false,&s->cleanup_status);s->down=s->cleanup_error==BLOCK_OK;
    if (!s->down) { io->poison(ctx,s->cleanup_error);if (r==BLOCK_OK) { r=s->cleanup_error; } }
    if (r==BLOCK_OK) { s->phase=6; }return r;
}
enum block_result brcm_join_probe_run(const struct brcm_join_probe_io *io,const struct brcm_auth_peer *peer,bool prerequisites,struct brcm_join_probe *s)
{
    if (!io || !io->policy.iovar || !io->station || !io->radio || !io->poll || !io->delay_us || !io->milliseconds || !io->poison || !io->station_live || !s) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }if (!prerequisites) { return BLOCK_NOT_READY; }
    unsigned char check[70];enum block_result r=brcm_join_encode(peer,check);if (r!=BLOCK_OK) { return r; }
    *s=(struct brcm_join_probe){.attempted=true,.phase=1,.peer=*peer,.error=BLOCK_NOT_READY,.policy={.error=BLOCK_NOT_READY}};
    r=io->station(io->policy.context,&s->firmware_status);if (r!=BLOCK_OK) { s->error=r;return r; }
    struct action_context action={io,s};r=brcm_auth_policy_events(&io->policy,true,&s->policy,io->extra_events,observe,&action);
    if (s->policy.changed && !s->policy.restored) { io->poison(io->policy.context,s->policy.restore_error); }
    s->error=r;if (r==BLOCK_OK) { s->phase=7; }return r;
}
