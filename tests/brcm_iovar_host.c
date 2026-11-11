#include <stdio.h>
#include <string.h>
#include "brcm_control.h"
#include "brcm_auth.h"
#include "brcm_join_probe.h"
#include "host_assert.h"
static struct brcm_control_state state;
static struct {
    unsigned calls,mode,position,bytes,tx,sequence,sets;
    unsigned station_fail_at,station_io_at,station_short_at,station_sets;
    unsigned identity_fault; /* 1/2 rejected, 3/4 short, 5 invalid SSID length */
    bool pending,echo_set,auth_model,station_model,ignore_station_set;
    uint32_t infra,ap,up,policy[4];unsigned char mask[24],response[2048];
} m;
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static enum block_result read32(void *ctx,uint32_t at,uint32_t *v)
{ (void)ctx;++m.calls;if (m.mode==3) { return BLOCK_IO; }assert(at==0x18004020 || at==0x1800404c);*v=at==0x18004020 ? (m.pending ? 0x40 : 0) : 0x40002;return BLOCK_OK; }
static enum block_result write32(void *ctx,uint32_t at,uint32_t v)
{ (void)ctx;++m.calls;assert(at==0x18004020 && v==0x40);return BLOCK_OK; }
static enum block_result delay(void *ctx,unsigned us) { (void)ctx;++m.calls;assert(us==10000);return BLOCK_OK; }
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    (void)ctx;++m.calls;unsigned char *p=buffer;
    if (write) {
        if (m.mode==2) { return BLOCK_IO; }
        assert(n<=2048 && !(n&3));struct brcm_sdpcm_header h;assert(brcm_sdpcm_header(p,n,&h)==BLOCK_OK && h.length+3u>=n && h.sequence==state.tx_seq);
        uint32_t command=get32(p+12),payload=get32(p+16),flags=get32(p+20);bool set=command==263;
        if (command==23 || command==25) {
            unsigned which=command==23 ? 1 : 2,wanted=command==23 ? 6 : 36;
            assert(!(flags&2) && payload==wanted && h.length==28+wanted && !get32(p+24));
            bool rejected=m.identity_fault==which;unsigned out=m.identity_fault==which+2 ? wanted-1 : wanted,len=28+out;
            memset(m.response,0,sizeof(m.response));put32(m.response,((uint32_t)(uint16_t)~len<<16)|len);
            m.response[4]=(unsigned char)m.sequence++;m.response[7]=12;m.response[9]=(uint8_t)(state.tx_seq+4);
            put32(m.response+12,command);put32(m.response+16,out);put32(m.response+20,(flags&0xffff0000u)|(rejected ? 1u : 0u));put32(m.response+24,rejected ? 0xffffffe9u : 0);
            if (command==23) { const unsigned char mac[6]={2,0,0,0,0,2};memcpy(m.response+28,mac,6); }
            else { put32(m.response+28,m.identity_fault==5 ? 33 : 12);memcpy(m.response+32,"public-probe",12); }
            m.bytes=(len+3)&~3u;m.position=0;m.pending=true;++m.tx;return BLOCK_OK;
        }
        if (command==19 || command==20 || command==117 || command==162) {
            bool setting=command==20;unsigned operation=m.tx+1;
            assert(!!(flags&2)==setting && payload==4 && h.length==32 && !get32(p+24));
            if (setting) { assert(m.station_model && get32(p+28)==1 && !m.ap && !m.up && !m.infra); }
            else { assert(!get32(p+28)); }
            if (m.station_io_at==operation) { ++m.tx;return BLOCK_IO; }
            bool rejected=m.mode==1 || m.station_fail_at==operation;
            if (setting && !rejected) { ++m.station_sets;if (!m.ignore_station_set) { m.infra=1; } }
            unsigned out=setting ? 0 : m.mode==8 || m.station_short_at==operation ? 3 : 4,len=28+out;memset(m.response,0,sizeof(m.response));
            put32(m.response,((uint32_t)(uint16_t)~len<<16)|len);m.response[4]=(unsigned char)m.sequence++;m.response[7]=12;m.response[9]=(uint8_t)(state.tx_seq+4);
            put32(m.response+12,command);put32(m.response+16,setting && m.echo_set ? 4 : out);put32(m.response+20,(flags&0xffff0000u)|(setting && m.echo_set ? 2u : 0u)|(rejected ? 1u : 0u));put32(m.response+24,rejected ? 0xffffffe9u : 0);
            uint32_t value=command==117 ? m.ap : command==162 ? m.up : m.station_model ? m.infra : m.mode==7 ? 0 : 1;
            if (!setting) { put32(m.response+28,value); }
            m.bytes=(len+3)&~3u;m.position=0;m.pending=true;++m.tx;return BLOCK_OK;
        }
        assert((command==262 || command==263) && !!(flags&2)==set && payload==h.length-28u && !get32(p+24));
        unsigned namelen=(unsigned)strlen((char *)p+28)+1;assert(namelen<=payload);
        if (!strcmp((char *)p+28,"cap")) { assert(!set && payload==772); }
        else if (!strcmp((char *)p+28,"cur_etheraddr")) { assert(!set && payload==20); }
        else if (!strcmp((char *)p+28,"mfp")) { assert(payload==8); }
        else if (!strcmp((char *)p+28,"auth") || !strcmp((char *)p+28,"wpa_auth") || !strcmp((char *)p+28,"wsec")) { assert(payload==namelen+4); }
        else if (!strcmp((char *)p+28,"event_msgs_ext")) {
            assert(payload==(set ? namelen+4u+p[30+namelen] : 274u) && p[28+namelen]==1);
            if (!set) { assert(p[29+namelen]==0 && p[30+namelen]==255 && p[31+namelen]==255); }
        }
        else if (!strcmp((char *)p+28,"event_msgs")) { assert(!set && payload==27); }
        else if (!strcmp((char *)p+28,"join")) { assert(set && payload==75 && get32(p+33)==12 && p[33+68]==6 && p[33+69]==16); }
        else if (!strcmp((char *)p+28,"mgmt_frame")) { assert(set && payload==147 && p[28+namelen+10]==0xb0 && get32(p+28+namelen+28)==0x12345678); }
        else if (!strcmp((char *)p+28,"wpaie")) { assert(set ? payload==namelen || payload==namelen+28 : payload==namelen+256); }
        else if (!strcmp((char *)p+28,"assoc_info") || !strcmp((char *)p+28,"assoc_req_ies")) { assert(!set && payload==namelen+512); }
        else if (!strcmp((char *)p+28,"auth_status")) { assert(set && payload==namelen+60 && p[28+namelen]==4 && !p[29+namelen]); }
        else { assert(!strcmp((char *)p+28,"clmload") && set && payload==1420 && n==1448);for (unsigned i=0;i<1412;++i) { assert(p[28+namelen+i]==(unsigned char)(i*19)); } }
        unsigned out=set ? 0 : payload,len=28+out;memset(m.response,0,sizeof(m.response));
        put32(m.response,((uint32_t)(uint16_t)~len<<16)|len);m.response[4]=(unsigned char)m.sequence++;m.response[7]=12;m.response[9]=(uint8_t)(state.tx_seq+4);
        put32(m.response+12,command);put32(m.response+16,m.mode==6 && set ? payload : m.mode==4 ? out+1 : out);
        put32(m.response+20,(flags&0xffff0000u)|(m.echo_set && set ? 2u : 0u)|(m.mode==1 ? 1u : m.mode==5 ? 2u : 0u));
        put32(m.response+24,m.mode==1 ? 0xffffffe9u : 0);
        for (unsigned i=0;i<out;++i) { m.response[28+i]=(unsigned char)(i*7); }
        if (m.auth_model) {
            static const char *const properties[]={"auth","wpa_auth","wsec","mfp"};unsigned index=4;
            for (unsigned i=0;i<4;++i) { if (!strcmp((char *)p+28,properties[i])) { index=i; } }
            if (index<4) {
                if (set) {
                    ++m.sets;uint32_t value=get32(p+28+namelen);
                    if (index==1 && m.policy[index]!=value) { m.policy[0]=2; }
                    m.policy[index]=value;if (index==2) { m.policy[3]=0; }
                }
                else { put32(m.response+28,m.policy[index]); }
            } else if (!strcmp((char *)p+28,"event_msgs_ext")) {
                if (set) { assert(payload==43 && p[29+namelen]==3 && p[30+namelen]==24 && !p[31+namelen]);memcpy(m.mask,p+32+namelen,24);++m.sets; }
                else {
                    /* Model the captured 274-byte padded CDC reply: the name's
                     * e/v bytes survive, len/maxgetsize are 24, mask is @4. */
                    memcpy(m.response+28,p+28,out);m.response[30]=24;m.response[31]=24;memcpy(m.response+32,m.mask,24);
                }
            } else { assert(!set && !strcmp((char *)p+28,"event_msgs"));memcpy(m.response+28,m.mask,16); }
        }
        m.bytes=(len+3)&~3u;m.position=0;m.pending=true;++m.tx;
    } else {
        assert(n<=512 && !(n&3));memset(p,0,n);
        if (!m.pending) { assert(n==64);return BLOCK_OK; }
        unsigned remain=m.bytes-m.position;assert((!m.position && n==64) || (m.position && n==(remain>512 ? 512 : remain)));
        unsigned copy=n<remain ? (unsigned)n : remain;memcpy(p,m.response+m.position,copy);m.position+=copy;
        if (m.position==m.bytes) { m.pending=false; }
    }return BLOCK_OK;
}
static void seed(void)
{ memset(&m,0,sizeof(m));state=(struct brcm_control_state){.ready=true,.f2_ready=true,.device_ready=true,.error=BLOCK_OK,.phase=CONTROL_READY,.tx_seq=2,.tx_max=22,.request_id=2}; }
static enum block_result auth_iovar(void *ctx,bool set,const char *name,const void *input,size_t bytes,void *output,size_t capacity,size_t *actual,uint32_t *status)
{ return brcm_control_iovar(ctx,&state,set,name,input,bytes,output,capacity,actual,status); }
int main(void)
{
    struct brcm_control_io io={.read32=read32,.write32=write32,.packet=packet,.delay_us=delay};
    unsigned char output[770],input[1412];size_t n;uint32_t status;
    seed();memset(output,0xa5,sizeof(output));
    assert(brcm_control_iovar(&io,&state,false,"cap",NULL,0,output,768,&n,&status)==BLOCK_OK && n==768 && !status && state.ready);
    for (unsigned i=0;i<768;++i) { assert(output[i]==(unsigned char)(i*7)); }assert(output[768]==0xa5 && output[769]==0xa5);
    assert(brcm_control_iovar(&io,&state,false,"cur_etheraddr",NULL,0,output,6,&n,&status)==BLOCK_OK && n==6);
    for (unsigned i=0;i<1412;++i) { input[i]=(unsigned char)(i*19); }
    assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK && !n);
    m.echo_set=true;assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK);
    m.mode=6;assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK && !n);
    seed();m.mode=1;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_UNSUPPORTED && status==0xffffffe9u && state.ready);
    m.mode=0;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_OK && n==4);
    m.mode=1;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_UNSUPPORTED && state.ready);
    m.mode=3;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_IO && !state.ready);
    for (unsigned mode=2;mode<=5;++mode) {
        seed();m.mode=mode;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)!=BLOCK_OK && !state.ready && !state.waiting);
        unsigned calls=m.calls;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_NOT_READY && m.calls==calls);
    }
    seed();state.tx_seq=255;state.tx_max=3;state.request_id=65535;m.sequence=255;state.rx_seq=255;
    assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_OK && state.tx_seq==0 && state.request_id==0 && state.rx_seq==0);
    seed();assert(brcm_control_iovar(&io,&state,true,"ssid",NULL,0,NULL,0,&n,&status)==BLOCK_UNSUPPORTED && !m.calls);
    assert(brcm_control_iovar(&io,&state,false,"cap",input,769,output,768,&n,&status)==BLOCK_ARGUMENT && !m.calls);
    unsigned char ext[259]={1,0,255,255},word[4]={3},auth[136]={0};
    assert(brcm_control_iovar(&io,&state,false,"event_msgs_ext",ext,259,output,259,&n,&status)==BLOCK_OK && n==259);
    ext[1]=3;ext[2]=25;ext[3]=0;
    assert(brcm_control_iovar(&io,&state,true,"event_msgs_ext",ext,29,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,true,"auth",word,4,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,false,"wpa_auth",NULL,0,output,4,&n,&status)==BLOCK_OK && n==4);
    auth[10]=0xb0;put32(auth+28,0x12345678);
    assert(brcm_control_iovar(&io,&state,true,"mgmt_frame",auth,136,NULL,0,&n,&status)==BLOCK_OK);
    unsigned char rsn[28]={0x30,26},success[60]={4};
    assert(brcm_control_iovar(&io,&state,true,"wpaie",rsn,28,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,false,"wpaie",NULL,0,output,256,&n,&status)==BLOCK_OK && n==256);
    assert(brcm_control_iovar(&io,&state,true,"wpaie",NULL,0,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,false,"assoc_info",NULL,0,output,512,&n,&status)==BLOCK_OK && n==512);
    assert(brcm_control_iovar(&io,&state,false,"assoc_req_ies",NULL,0,output,512,&n,&status)==BLOCK_OK && n==512);
    assert(brcm_control_iovar(&io,&state,true,"auth_status",success,60,NULL,0,&n,&status)==BLOCK_OK);
    unsigned before=m.calls;assert(brcm_control_iovar(&io,&state,true,"auth_status",success,59,NULL,0,&n,&status)==BLOCK_ARGUMENT && m.calls==before);
    assert(brcm_control_iovar(&io,&state,false,"auth_status",NULL,0,output,60,&n,&status)==BLOCK_UNSUPPORTED && m.calls==before);
    assert(brcm_control_iovar(&io,&state,false,"wpaie",NULL,0,output,255,&n,&status)==BLOCK_ARGUMENT && m.calls==before);
    assert(brcm_control_iovar(&io,&state,true,"assoc_info",NULL,0,NULL,0,&n,&status)==BLOCK_UNSUPPORTED && m.calls==before);
    assert(brcm_control_iovar(&io,&state,true,"assoc_req_ies",NULL,0,NULL,0,&n,&status)==BLOCK_UNSUPPORTED && m.calls==before);
    assert(brcm_control_iovar(&io,&state,false,"assoc_info",NULL,0,output,511,&n,&status)==BLOCK_ARGUMENT && m.calls==before);
    assert(brcm_control_iovar(&io,&state,false,"assoc_req_ies",word,1,output,512,&n,&status)==BLOCK_ARGUMENT && m.calls==before);
    struct brcm_link_identity identity;
    seed();assert(brcm_control_identity(&io,&state,&identity,&status)==BLOCK_OK && identity.bssid[0]==2 && identity.bssid[5]==2 && identity.ssid_length==12 && !memcmp(identity.ssid,"public-probe",12) && m.tx==2);
    for (unsigned fault=1;fault<=5;++fault) {
        seed();m.identity_fault=fault;assert(brcm_control_identity(&io,&state,&identity,&status)!=BLOCK_OK && !identity.ssid_length && !identity.bssid[0]);
    }
    seed();m.mode=2;assert(brcm_control_identity(&io,&state,&identity,&status)==BLOCK_IO && !state.ready);
    puts("PASS: fixed auth_status/wpaie CDC transport and bounds; read-only BSSID/SSID command pair, rejected/truncated replies, invalid lengths and I/O poisoning");
    puts("PASS: extended-mask GET input header, policy IOVARs and management-frame SET transported through actual CDC/SDPCM framing");
    puts("PASS: restricted IOVAR GET/SET framing, 1448-byte CLM TX, 800-byte multi-part RX, padded results/copy bounds, SET reply variants and ID/sequence wrap");
    puts("PASS: rejected firmware request preserves transport, later I/O failure poisons it; no access after failure and no arbitrary SET interface");
    seed();m.auth_model=true;m.policy[3]=1;m.mask[0]=0xa5;m.mask[23]=0x80;
    const struct brcm_auth_probe_io auth_io={&io,auth_iovar};struct brcm_auth_probe probe={0};
    assert(brcm_auth_probe_run(&auth_io,true,&probe)==BLOCK_OK && probe.verified && probe.restored && probe.mask_compatibility && probe.mask_legacy_verified && probe.mask_snapshot_stable);
    assert(probe.mask_reply_bytes==259 && probe.mask_header[0]==101 && probe.mask_header[1]==118 && probe.mask_bytes==24 && m.sets==10 && m.tx==34 && probe.auth_trace_mask==15 && probe.auth_after[1]==2 && probe.auth_after[0]==3 && probe.mask_readback_verified);
    assert(!m.policy[0] && !m.policy[1] && !m.policy[2] && m.policy[3]==1 && m.mask[0]==0xa5 && m.mask[23]==0x80 && state.ready);
    puts("PASS: captured e/v padded GET header through real CDC/SDPCM transport, legacy/stable checks, SAE/PMF/event readback and exact full restoration");
    seed();assert(brcm_control_station(&io,&state,&status)==BLOCK_OK && state.station_mode_known && state.station_mode==1);
    seed();m.mode=7;assert(brcm_control_station(&io,&state,&status)==BLOCK_UNSUPPORTED && state.ready && state.station_mode_known && !state.station_mode);
    seed();m.mode=8;assert(brcm_control_station(&io,&state,&status)==BLOCK_RANGE && !state.station_mode_known);
    seed();m.mode=1;assert(brcm_control_station(&io,&state,&status)==BLOCK_UNSUPPORTED && state.ready && status==0xffffffe9u);
    seed();m.mode=3;assert(brcm_control_station(&io,&state,&status)==BLOCK_IO && !state.ready);
    seed();struct brcm_auth_peer target={.local={2,0,0,0,0,1},.peer={2,0,0,0,0,2},.ssid="public-probe",.ssid_length=12,.channel=6,.band=2};unsigned char join[70];
    assert(brcm_join_encode(&target,join)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,true,"join",join,70,NULL,0,&n,&status)==BLOCK_OK && m.tx==1);
    unsigned calls=m.calls;
    assert(brcm_control_iovar(&io,&state,true,"join",join,69,NULL,0,&n,&status)==BLOCK_ARGUMENT && m.calls==calls);
    assert(brcm_control_iovar(&io,&state,false,"join",NULL,0,output,4,&n,&status)==BLOCK_UNSUPPORTED && m.calls==calls);
    puts("PASS: read-only INFRA query/STA gate, unsupported/short/I/O paths, fixed 70-byte join SET through CDC/SDPCM and malformed/no-GET refusal");
    seed();m.station_model=true;m.echo_set=true;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_OK && m.tx==5 && m.station_sets==1 && m.infra==1 && !m.up);
    assert(state.station_prepare_phase==6 && state.station_initial_known && !state.station_initial_mode && state.station_mode_changed && state.station_change_attempted && state.station_mode_known && state.station_mode==1);
    seed();m.station_model=true;m.infra=1;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_OK && m.tx==3 && !m.station_sets && !state.station_change_attempted && state.station_mode==1);
    seed();m.station_model=true;m.ap=1;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_UNSUPPORTED && m.tx==1 && !m.station_sets && !state.station_change_attempted);
    seed();m.station_model=true;m.up=1;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_NOT_READY && m.tx==2 && !m.station_sets && !state.station_change_attempted);
    seed();m.station_model=true;m.infra=2;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_UNSUPPORTED && m.tx==3 && !m.station_sets);
    seed();m.station_model=true;m.ignore_station_set=true;
    assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_IO && m.tx==5 && m.station_sets==1 && state.station_prepare_phase==5 && state.station_mode_known && !state.station_mode && !state.station_mode_changed);
    for (unsigned fail=1;fail<=5;++fail) {
        seed();m.station_model=true;m.station_fail_at=fail;
        assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_UNSUPPORTED && m.tx==fail && status==0xffffffe9u && state.ready && !state.station_mode_changed);
        if (fail<=4) { assert(!m.station_sets); }else { assert(m.infra==1 && !state.station_mode_known); }
        seed();m.station_model=true;m.station_io_at=fail;
        assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_IO && m.tx==fail && !state.ready && !state.station_mode_changed);
        unsigned before=m.calls;assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_NOT_READY && before==m.calls);
    }
    const unsigned short_at[]={1,2,3,5};
    for (unsigned i=0;i<4;++i) {
        seed();m.station_model=true;m.station_short_at=short_at[i];
        assert(brcm_control_station_prepare(&io,&state,&status)==BLOCK_RANGE && m.tx==short_at[i] && !state.station_mode_changed);
        if (short_at[i]<4) { assert(!m.station_sets); }
    }
    puts("PASS: captured INFRA=0 station initialization via real CDC/SDPCM; AP/UP/unknown-role gates, already-STA reuse, verified SET_INFRA=1, 10 command failures and four truncated replies without radio UP/join");return 0;
}
