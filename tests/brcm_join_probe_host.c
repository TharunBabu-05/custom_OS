#include <stdio.h>
#include <string.h>
#include "brcm_join_probe.h"
#include "host_assert.h"
static const struct brcm_auth_peer peer={.local={2,0,0,0,0,1},.peer={2,0,0,0,0,2},.ssid="public-probe",.ssid_length=12,.channel=6,.band=2};
static struct brcm_join_probe state;
static struct {
    uint32_t values[4],infra;unsigned char mask[24];unsigned calls,sets,joins,ups,downs,polls,fail_at,mode,poisons;
    uint64_t clock;bool up,poisoned,alias,emitted;
} m;
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void be16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void be32(unsigned char *p,unsigned v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(24-8*i)); } }
static enum block_result touch(uint32_t *status)
{ ++m.calls;if (status) { *status=0; }if (m.poisoned) { return BLOCK_NOT_READY; }if (m.calls==m.fail_at) { if (status) { *status=0xffffffe9u; }return BLOCK_IO; }return BLOCK_OK; }
static void emit(void)
{
    unsigned char frame[136]={0};frame[0]=0x20;unsigned char *p=frame+4;
    be16(p+12,0x886c);be16(p+14,0x8001);p[20]=16;p[21]=24;be16(p+22,1);be16(p+24,2);be32(p+28,187);be32(p+44,60);
    unsigned char *d=p+72;put16(d,0);memcpy(d+2,peer.peer,6);put32(d+8,peer.ssid_length);memcpy(d+12,peer.ssid,peer.ssid_length);
    if (m.mode==3 || m.mode==15) { d[2]^=2; }if (m.mode==4) { d[12]^=1; }if (m.mode==5) { be32(p+32,1); }
    brcm_join_event(&state,frame,m.mode==6 ? 3 : sizeof(frame));m.emitted=true;
}
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t bytes,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    (void)ctx;*actual=0;enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    if (set && m.ups && strcmp(name,"join")) { assert(!m.up); } /* DOWN must precede restoration. */
    if (!m.ups) { assert(m.infra==1); }
    static const char *const names[]={"auth","wpa_auth","wsec","mfp"};unsigned index=4;
    for (unsigned i=0;i<4;++i) { if (!strcmp(name,names[i])) { index=i; } }
    if (index<4) {
        if (set) {
            assert(bytes==4 && !output && !capacity);++m.sets;uint32_t v=get32(input);
            m.values[index]=index==0 && v==3 && m.alias ? (m.mode==10 ? 0 : 2) : v;
            if (index==2) { m.values[3]=0; }
            if (index==3) { if (m.mode==8 && v==2) { m.values[3]=1; }m.values[2]=(m.values[2]&~0x600u)|(m.values[3]==2 ? 0x600u : m.values[3]==1 ? 0x200u : 0); }
        } else {
            assert(capacity==4 && !input && !bytes);uint32_t v=m.values[index];
            if (m.sets==5 && m.mode==7 && index==1) { v|=0x80; }
            if (m.sets==5 && m.mode==9 && index==2) { v|=2; }
            put32(output,v);*actual=4;
        }
    } else if (!strcmp(name,"event_msgs_ext")) {
        const unsigned char *p=input;assert(p && p[0]==1);
        if (set) { assert(bytes==28 && p[1]==3 && p[2]==24);memcpy(m.mask,p+4,24);++m.sets; }
        else { assert(bytes==259 && capacity==259);unsigned char *d=output;memset(d,0,259);d[0]='e';d[1]='v';d[2]=d[3]=24;memcpy(d+4,m.mask,24);*actual=259; }
    } else if (!strcmp(name,"event_msgs")) { assert(!set && capacity==16 && !input && !bytes);memcpy(output,m.mask,16);*actual=16; }
    else {
        assert(!strcmp(name,"join") && set && bytes==70 && !output && !capacity && m.up && !m.joins);
        unsigned char expected[70];assert(brcm_join_encode(&peer,expected)==BLOCK_OK && !memcmp(input,expected,70));++m.joins;
        assert(m.values[1]==0x40000 && m.values[2]==0x604 && m.values[3]==2 && (m.mask[23]&0x78)==0x78);
        if (m.mode==1 || m.mode==14) { emit(); }
        if (m.mode==14) { return BLOCK_IO; }
    }
    return BLOCK_OK;
}
static enum block_result station(void *ctx,uint32_t *status)
{ (void)ctx;enum block_result r=touch(status);if (m.mode==17) { emit();assert(!state.request_seen && !state.events); }if (r==BLOCK_OK && m.mode!=12) { m.infra=1; }return r!=BLOCK_OK ? r : m.mode==12 ? BLOCK_UNSUPPORTED : BLOCK_OK; }
static enum block_result station_live(void *ctx,uint32_t *status)
{ (void)ctx;enum block_result r=touch(status);return r!=BLOCK_OK ? r : m.infra==1 ? BLOCK_OK : BLOCK_UNSUPPORTED; }
static enum block_result radio(void *ctx,bool up,uint32_t *status)
{
    (void)ctx;if (up) { ++m.ups; }else { ++m.downs; }
    enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    if (!up && m.mode==11) { *status=0xffffffe9u;return BLOCK_IO; }
    m.up=up;
    if (up && m.mode==18) { m.values[1]|=0x80; }
    if (up && m.mode==19) { m.mask[23]&=(unsigned char)~0x78; }
    if (up && m.mode==20) { m.infra=0; }
    return BLOCK_OK;
}
static enum block_result poll(void *ctx)
{
    (void)ctx;++m.polls;enum block_result r=touch(NULL);if (r!=BLOCK_OK) { return r; }
    if (!m.emitted && m.mode!=2 && m.mode!=13) { emit();if (m.mode==15) { for (unsigned i=0;i<256;++i) { emit(); } }if (m.mode==16) { emit();assert(state.events==1); } }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ (void)ctx;assert(us==10000);enum block_result r=touch(NULL);if (r==BLOCK_OK && m.mode!=13) { m.clock+=10; }return r; }
static uint64_t milliseconds(void *ctx) { (void)ctx;return m.clock; }
static void poison(void *ctx,enum block_result r) { (void)ctx;assert(r!=BLOCK_OK);m.poisoned=true;++m.poisons; }
static const struct brcm_join_probe_io io={{NULL,iovar},station,radio,poll,delay,milliseconds,poison,station_live,NULL,0};
static void seed(void)
{ memset(&state,0,sizeof(state));memset(&m,0,sizeof(m));m.alias=true;m.values[2]=0x200;m.values[3]=1;m.mask[0]=0xa5;m.mask[23]=0x80; }
static void restored(void)
{ assert(state.policy.restored && !m.up && !m.values[0] && !m.values[1] && m.values[2]==0x200 && m.values[3]==1 && m.mask[0]==0xa5 && m.mask[23]==0x80); }
int main(void)
{
    seed();unsigned char encoded[72];memset(encoded,0xa5,sizeof(encoded));assert(brcm_join_encode(&peer,encoded)==BLOCK_OK && encoded[70]==0xa5);
    assert(get32(encoded)==12 && !memcmp(encoded+4,peer.ssid,12) && !encoded[36] && !encoded[37] && !encoded[38] && !encoded[39]);
    for (unsigned at=40;at<56;at+=4) { assert(get32(encoded+at)==UINT32_MAX); }
    assert(!memcmp(encoded+56,peer.peer,6) && !encoded[62] && !encoded[63] && get32(encoded+64)==1 && encoded[68]==6 && encoded[69]==16);
    struct brcm_auth_peer bad=peer;bad.ssid_length=0;assert(brcm_join_probe_run(&io,&bad,true,&state)==BLOCK_ARGUMENT && !m.calls);
    bad=peer;bad.band=5;assert(brcm_join_probe_run(&io,&bad,true,&state)==BLOCK_UNSUPPORTED && !m.calls);
    assert(brcm_join_probe_run(&io,&peer,false,&state)==BLOCK_NOT_READY && !m.calls && !state.attempted);
    assert(brcm_join_probe_run(&io,&peer,true,&state)==BLOCK_OK && state.request_seen && state.down && state.policy.external_request_verified && state.policy.auth_readback_pending && state.policy.mismatch_mask==1 && state.policy.observed[2]==0x604);restored();
    unsigned total=m.calls;assert(total==45 && m.infra==1 && state.live_mask_verified && state.live_read_mask==15);assert(brcm_join_probe_run(&io,&peer,true,&state)==BLOCK_OK && m.calls==total);
    seed();m.alias=false;assert(brcm_join_probe_run(&io,&peer,true,&state)==BLOCK_OK && !state.policy.auth_readback_pending && !state.policy.mismatch_mask);restored();
    for (unsigned mode=1;mode<=20;++mode) {
        seed();m.mode=mode;enum block_result r=brcm_join_probe_run(&io,&peer,true,&state);
        if (mode==1 || mode==16) { assert(r==BLOCK_OK && state.request_seen && (mode!=1 || !m.polls));restored(); }
        else {
            assert(r!=BLOCK_OK && !state.policy.external_request_verified);
            if (mode==11) { assert(m.poisoned && !state.down && !state.policy.restored && state.cleanup_status==0xffffffe9u); }
            else if (mode==12) { assert(!state.policy.changed && !m.sets && !m.ups && !m.joins); }
            else { restored(); }
            if (mode==7 || mode==8 || mode==9 || mode==10) { assert(!m.ups && !m.joins); }
            if (mode==2 || mode==3 || mode==4 || mode==6 || mode==13 || mode==17) { assert(r==BLOCK_TIMEOUT && m.polls<=1500); }
            if (mode==15) { assert(r==BLOCK_RANGE && state.events==257); }
            if (mode==18 || mode==19 || mode==20) { assert(!m.joins && m.ups && state.down); }
            if (mode==14) { assert(state.request_seen && state.down && r==BLOCK_IO); }
        }
        unsigned calls=m.calls;(void)brcm_join_probe_run(&io,&peer,true,&state);assert(m.calls==calls);
    }
    seed();m.mode=2;m.fail_at=34;
    assert(brcm_join_probe_run(&io,&peer,true,&state)==BLOCK_IO && state.down && state.polls==1 && !m.clock);restored();
    for (unsigned fail=1;fail<=total;++fail) {
        seed();m.fail_at=fail;assert(brcm_join_probe_run(&io,&peer,true,&state)!=BLOCK_OK);
        if (m.ups) { assert(m.downs==1); }
        if (state.policy.changed) { assert(state.policy.restored || m.poisoned); }
        assert(!state.armed);unsigned calls=m.calls;(void)brcm_join_probe_run(&io,&peer,true,&state);assert(calls==m.calls);
    }
    puts("PASS: exact 70-byte bounded ext_join, captured auth=2/wsec=0x604, peer/SSID-bound external SAE events before/after ACK, no credentials/SAE TX/key installation");
    puts("PASS: STA initialization/live-role recheck, policy gates, role/event resets after UP, timeouts/stalled clocks, event errors, 45 per-operation failures, DOWN-before-restore, poisoning and one-shot refusal");return 0;
}
