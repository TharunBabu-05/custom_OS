#include <stdio.h>
#include <string.h>
#include "brcm_sae_probe.h"
#include "brcm_assoc.h"
#include "p256.h"
#include "sae_hunt.h"
#include "aes.h"
#include "crypto.h"
#include "host_assert.h"
#include "dhcp_fixture.h"
#include "netprobe_fixture.h"
/* Public fixtures only. The model acts as an AP using the SAE engine; its
 * deterministic randomness never enters a kernel/hardware path. */
static struct brcm_auth_peer peer={.local={2,0,0,0,0,1},.peer={2,0,0,0,0,2},.ssid="public-probe",.ssid_length=12,.channel=6,.band=2};
static unsigned char own[98],remote[98];
static struct brcm_sae_probe plain_state;
static struct brcm_assoc_probe assoc;
static struct brcm_sae_probe *selected=&plain_state;
#define state (*selected)
static struct sae_session client,ap;
static unsigned char ap_confirm[34];
enum auth_case { AUTH_NORMAL,AUTH_EARLY_RX,AUTH_EARLY_ALL,AUTH_BAD_HMAC,AUTH_WRONG_PWE,AUTH_NO_RX,AUTH_NACK,AUTH_SET_FAIL,AUTH_STALE_COOKIE,AUTH_SLOW_MATH,AUTH_CHANGED_COMMIT,
    AUTH_COMMIT_LOST_ONCE,AUTH_COMMIT_LOST_TWICE,AUTH_COMMIT_NO_RX,AUTH_COMMIT_NO_ACK,AUTH_COMMIT_STALLED,AUTH_RETRY_SET_FAIL,AUTH_RETRY_STALE_ACK,AUTH_RETRY_NACK,AUTH_RETRY_IO,AUTH_EARLY_FLOOD,AUTH_COUNT };
enum mode { NORMAL,EARLY,RX_FIRST,NACK,SET_FAIL,NO_ACK,NO_RX,OFF_ONLY,BAD_POINT,REFLECTION,
    TOKEN_NEEDED,EXTENDED,NEW_METADATA,WRONG_PEER,WRONG_CHANNEL,WRONG_COOKIE,STALLED,STORM,DOWN_FAIL,OFF_FAIL,DUPLICATE,METADATA_V1,MODE_COUNT };
enum assoc_case { ASSOC_NORMAL,ASSOC_EARLY,ASSOC_NO_EVENT,ASSOC_NO_M1,ASSOC_WRONG_PEER,ASSOC_WRONG_IF,
    ASSOC_REJECT,ASSOC_DEAUTH,ASSOC_WRONG_ID,ASSOC_ZERO_ID,ASSOC_DOWNGRADE,ASSOC_BAD_M1,ASSOC_WPA2_M1,
    ASSOC_STATUS_FAIL,ASSOC_IE_IGNORE,ASSOC_IE_BAD_SNAPSHOT,ASSOC_IE_RESTORE_FAIL,ASSOC_IE_RESET_UP,
    ASSOC_STALLED,ASSOC_BAD_SAE,ASSOC_OLD_IE,ASSOC_DATA_PADDING,ASSOC_COUNT };
enum owner_case { OWNER_NORMAL,OWNER_OPAQUE,OWNER_GET_UNSUPPORTED,OWNER_INFO_SHORT,OWNER_INFO_LARGE,OWNER_INFO_EMPTY,
    OWNER_REQ_SHORT,OWNER_NO_SSID,OWNER_BAD_SSID,OWNER_DUP_RSN,OWNER_WEAK_RSN,OWNER_BAD_PMKID,OWNER_CLEAR_FAIL,OWNER_IGNORE_SET,OWNER_SET_FAIL,OWNER_INFO_FAIL,OWNER_DEAUTH_IN_QUERY,OWNER_COUNT };
enum key_case { KEY_NORMAL,KEY_EARLY,KEY_WRITE_FAIL,KEY_NO_M3,KEY_BAD_MIC,KEY_BAD_WRAP,KEY_REPLAY,KEY_NONCE,KEY_PROFILE,KEY_RNG_FAIL,KEY_DEAUTH,KEY_WRONG_PEER,KEY_COUNT };
/* Exact public request bytes from the physical capture: 118 IE bytes + 4
 * bytes wrongly included by the old assoc_info ABI length interpretation. */
static const char captured_hex[]="00064a61636b2773010882848b962430486c32040c121860210203142402010d301a0100000fac040100000fac040100000fac08c0000000000fac062d1a210017ff000000000000000000000000000000000000000000007f080000080000000040dd09001018020000100000dd070050f20200010000000000";
static void captured(unsigned char *out)
{ for (unsigned i=0;i<122;++i) { unsigned x;assert(sscanf(captured_hex+2*i,"%2x",&x)==1);out[i]=(unsigned char)x; } }
static const unsigned char ap_rsn[]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,2,0,15,172,8,128,0};
static struct {
    uint32_t values[4];unsigned char mask[24];unsigned calls,sets,joins,tx,ups,downs,polls,tx_polls,fail_at,poisons;
    unsigned commit_txs,confirm_txs;uint32_t last_cookie;uint64_t last_commit_clock;
    enum mode mode;enum auth_case auth_case;uint64_t clock;bool up,poisoned,requested,fail_delay,auth,confirm_event;
    enum assoc_case assoc_case;bool association,status_sent,in_callback;
    unsigned assoc_polls,status_sets;unsigned char ie[256],original_ie[256];size_t ie_bytes,original_bytes;
    bool ie_owner;enum owner_case owner_case;unsigned baseline_sets,profile_sets,request_queries;
    bool wrong_method;
    bool fixed_request,captured_request,key_probe,m3_emitted;unsigned info_flags,fixed_short,m2_txs;enum key_case key_case;
    unsigned char m3[256];size_t m3_bytes;bool key_data_probe;unsigned kde_mode;
    bool install_probe;unsigned install_case,key_sets,clear_sets,m4_txs,m4_calls,retry_events;unsigned char expected_tk[16],expected_kck[16],expected_kek[16];
    bool dhcp_probe;unsigned dhcp_case,dhcp_txs,dhcp_calls,port_case,port_opens,port_closes,peer_queries;bool authorized;
    bool gateway_probe;unsigned gateway_case,net_calls,net_writes,declines,net_fail_points[8];
} m;
static void le16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void le32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void be16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void be32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(24-8*i)); } }
static void emit(unsigned code,uint32_t status,uint32_t cookie)
{
    unsigned char f[224]={0};f[0]=0x20;unsigned char *p=f+4,*d=p+72;
    be16(p+12,0x886c);be16(p+14,0x8001);p[20]=16;p[21]=24;be16(p+22,1);be16(p+24,2);
    be32(p+28,code);be32(p+32,status);memcpy(p+48,peer.peer,6);unsigned n=4;
    if (code==187) { n=60;memcpy(d+2,peer.peer,6);le32(d+8,peer.ssid_length);memcpy(d+12,peer.ssid,peer.ssid_length); }
    else if (code==188) {
        n=144;be16(d+2,0x1006);le16(d+40,3);le16(d+42,1);le16(d+44,peer.hunting^m.wrong_method ? 0 : 126);memcpy(d+46,remote,98);
        if (m.confirm_event) { n=80;le16(d+42,2);le16(d+44,0);memcpy(d+46,ap_confirm,34);if (m.auth_case==AUTH_BAD_HMAC) { d[79]^=1; } }
        if (m.mode==WRONG_PEER) { p[48]^=2; }
        if (m.mode==WRONG_CHANNEL) { be16(d+2,0x100b); }
        if (m.mode==TOKEN_NEEDED) { le16(d+44,76); }
        if (m.mode==EXTENDED) { ++n; }
        if (m.mode==NEW_METADATA) { be16(d,2); }
        if (m.mode==METADATA_V1) { be16(d,1); }
    }else if (code<187) { n=0;be16(p+26,code==16 ? 1 : 0);be32(p+36,status ? 17 : 0); }
    else { le32(d,cookie); }
    if (m.association && assoc.armed && code<187) {
        if (m.assoc_case==ASSOC_WRONG_PEER) { p[48]^=2; }
        if (m.assoc_case==ASSOC_WRONG_IF) { p[70]=1; }
    }
    be32(p+44,n);m.in_callback=true;
    if (m.association) { brcm_assoc_event(&assoc,f,76+n); }else { brcm_sae_probe_event(&state,f,76+n); }
    m.in_callback=false;
}
static void data_frame(void)
{
    unsigned char p[128]={0};p[0]=0x20;size_t offset=m.assoc_case==ASSOC_DATA_PADDING ? 8 : 4;p[3]=(unsigned char)((offset-4)/4);
    unsigned char *ether=p+offset;memcpy(ether,peer.local,6);memcpy(ether+6,peer.peer,6);be16(ether+12,0x888e);
    unsigned char *e=ether+14;e[0]=2;e[1]=3;be16(e+2,95);unsigned char *k=e+4;k[0]=2;be16(k+1,0x88);be16(k+3,16);k[12]=1;memset(k+13,0x42,32);
    if (m.assoc_case==ASSOC_BAD_M1) { be16(e+2,96); }
    if (m.assoc_case==ASSOC_WPA2_M1) { be16(k+1,0x8a); }
    m.in_callback=true;brcm_assoc_data(&assoc,p,offset+113);m.in_callback=false;
}
static void association_events(void)
{
    if (m.assoc_case==ASSOC_STALLED) { return; }
    if (m.assoc_case!=ASSOC_NO_EVENT) { emit(0,m.assoc_case==ASSOC_REJECT ? 1 : 0,0);emit(16,0,0); }
    if (m.assoc_case==ASSOC_DEAUTH) { emit(6,1,0); }
    if (m.assoc_case!=ASSOC_NO_M1) { data_frame(); }
}
static void m3_event(void)
{
    m.m3_emitted=true;if (m.key_case==KEY_DEAUTH) { emit(6,1,0);return; }
    if (m.key_case==KEY_WRONG_PEER) { m.m3[10]^=2; }
    m.in_callback=true;brcm_assoc_data(&assoc,m.m3,m.m3_bytes);m.in_callback=false;
}
static void retry_event(unsigned fault)
{
    ++m.retry_events;unsigned char *e=m.m3+18;
    if (fault!=5) { for (unsigned i=17;i-->9;) { if (++e[i]) { break; } } }
    if (fault==2) {
        unsigned char plain[512];size_t n=0;assert(aes128_unwrap(m.expected_kek,e+99,m.m3_bytes-117,plain,sizeof(plain),&n));plain[34]^=1;
        /* Alter a byte inside GTK, then rewrap below with the public model. */
        unsigned char wrapped[520];memset(wrapped,0xa6,8);memcpy(wrapped+8,plain,n);unsigned blocks=(unsigned)n/8;
        for (unsigned j=0;j<6;++j) { for (unsigned i=1;i<=blocks;++i) { unsigned char b[16];memcpy(b,wrapped,8);memcpy(b+8,wrapped+8*i,8);assert(aes128_encrypt(m.expected_kek,b,b));memcpy(wrapped,b,8);unsigned t=blocks*j+i;wrapped[6]^=(unsigned char)(t>>8);wrapped[7]^=(unsigned char)t;memcpy(wrapped+8*i,b+8,8); } }
        memcpy(e+99,wrapped,n+8);
    }
    if (fault==3) { e[65]^=1; }if (fault==4) { e[17]^=1; }
    memset(e+81,0,16);assert(aes128_cmac(m.expected_kck,e,m.m3_bytes-18,e+81));if (fault==1 || fault==5) { e[81]^=1; }
    m3_event();
}
static void events(void)
{
    emit(189,5,m.last_cookie+17); /* A foreign cookie must not poison this TX. */
    if (m.mode==RX_FIRST) { emit(188,0,1);assert(state.rx_structural && !state.tx_status_seen); }
    if (m.mode!=NO_ACK && m.mode!=OFF_ONLY && m.mode!=STALLED) { emit(189,m.mode==NACK ? 5 : 0,m.mode==WRONG_COOKIE ? m.last_cookie+1 : m.last_cookie); }
    emit(190,m.mode==OFF_FAIL ? 1 : 0,1);
    if (m.mode!=RX_FIRST && m.mode!=NO_RX && m.mode!=OFF_ONLY && m.mode!=STALLED) { emit(188,0,1); }
    if (m.mode==DUPLICATE) { emit(188,0,1); }
}
static void confirm_events(void)
{
    emit(189,5,m.last_cookie-1); /* Old commit NACK must not become confirm NACK. */
    emit(189,m.auth_case==AUTH_NACK ? 5 : 0,m.auth_case==AUTH_STALE_COOKIE ? m.last_cookie+1 : m.last_cookie);
    if (m.auth_case!=AUTH_NO_RX) { m.confirm_event=true;emit(188,0,2);m.confirm_event=false; }
}
static enum block_result touch(uint32_t *status)
{
    ++m.calls;if (status) { *status=0; }if (m.poisoned) { return BLOCK_NOT_READY; }
    if (m.calls==m.fail_at) { if (status) { *status=0xffffffe9u; }return BLOCK_IO; }return BLOCK_OK;
}
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t bytes,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    assert(ctx==&m && !m.in_callback);*actual=0;enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    if (set && m.ups && strcmp(name,"join") && strcmp(name,"mgmt_frame") && strcmp(name,"auth_status")) { assert(!m.up); }
    static const char *const names[]={"auth","wpa_auth","wsec","mfp"};unsigned index=4;
    for (unsigned i=0;i<4;++i) { if (!strcmp(name,names[i])) { index=i; } }
    if (index<4) {
        if (set) {
            assert(bytes==4 && !output && !capacity);++m.sets;uint32_t v=get32(input);
            m.values[index]=index==0 && v==3 ? 2 : v;
            if (index==2) { m.values[3]=0; }
            if (index==3) { m.values[2]=(m.values[2]&~0x600u)|(v==2 ? 0x600u : v==1 ? 0x200u : 0); }
        } else { assert(capacity==4 && !input && !bytes);le32(output,m.values[index]);*actual=4; }
    }else if (!strcmp(name,"event_msgs_ext")) {
        const unsigned char *p=input;assert(p && p[0]==1);
        if (set) { assert(bytes==28 && p[1]==3 && p[2]==24);memcpy(m.mask,p+4,24);++m.sets; }
        else { assert(bytes==259 && capacity==259);unsigned char *d=output;memset(d,0,259);d[0]='e';d[1]='v';d[2]=d[3]=24;memcpy(d+4,m.mask,24);*actual=259; }
    }else if (!strcmp(name,"event_msgs")) { assert(!set && capacity==16 && !input && !bytes);memcpy(output,m.mask,16);*actual=16; }
    else if (!strcmp(name,"wpaie")) {
        assert(m.association);
        if (set) {
            assert(!m.up && !output && !capacity && bytes<=256);
            if (m.ie_owner) {
                if (!bytes && !m.downs) { ++m.baseline_sets; }
                if (bytes) { ++m.profile_sets;if (m.owner_case==OWNER_SET_FAIL) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; } }
                if (m.downs && m.owner_case==OWNER_CLEAR_FAIL) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
                if (bytes && m.owner_case==OWNER_IGNORE_SET) { return BLOCK_OK; }
            }
            if (m.downs && m.assoc_case==ASSOC_IE_RESTORE_FAIL) { *status=0xffffffe9u;return BLOCK_IO; }
            if (m.assoc_case!=ASSOC_IE_IGNORE || m.downs) { if (bytes) { memcpy(m.ie,input,bytes); }m.ie_bytes=bytes; }
        }else {
            assert(!input && !bytes && capacity==256);memset(output,0,256);memcpy(output,m.ie,m.ie_bytes);*actual=256;
            if (m.assoc_case==ASSOC_IE_BAD_SNAPSHOT && !m.ups) { ((unsigned char *)output)[0]='x'; }
            if (m.ie_owner) {
                assert(!m.baseline_sets && !m.profile_sets && !m.ups);
                if (m.owner_case==OWNER_OPAQUE) { memcpy(output,"wpaie",6); }
                if (m.owner_case==OWNER_GET_UNSUPPORTED) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
            }
        }
    }
    else if (!strcmp(name,"assoc_info")) {
        assert(m.ie_owner && !set && !bytes && capacity==512 && !m.in_callback && m.status_sent);memset(output,0,512);
        ++m.request_queries;
        if (m.owner_case==OWNER_INFO_FAIL) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
        size_t count=2+peer.ssid_length+m.ie_bytes;
        if (m.owner_case==OWNER_NO_SSID) { count=m.ie_bytes; }
        if (m.owner_case==OWNER_DUP_RSN) { count+=m.ie_bytes; }
        if (m.owner_case==OWNER_BAD_PMKID) { count+=16; }
        if (m.owner_case==OWNER_INFO_LARGE) { count=513; }
        if (m.owner_case==OWNER_INFO_EMPTY && m.request_queries<3) { count=0; }
        if (m.captured_request) { count=118; }if (m.fixed_request) { count+=4; }
        le32(output,m.fixed_short ? m.fixed_short : (uint32_t)count);le32((unsigned char *)output+8,m.info_flags);
        *actual=m.owner_case==OWNER_INFO_SHORT ? 7 : 512;
    }
    else if (!strcmp(name,"assoc_req_ies")) {
        assert(m.ie_owner && !set && !bytes && capacity==512 && !m.in_callback && m.status_sent);unsigned char *p=output;memset(p,0,512);size_t at=0;
        if (m.owner_case!=OWNER_NO_SSID) { p[1]=(unsigned char)peer.ssid_length;memcpy(p+2,peer.ssid,peer.ssid_length);at=2+peer.ssid_length; }
        memcpy(p+at,m.ie,m.ie_bytes);
        if (m.owner_case==OWNER_BAD_SSID) { p[2]^=1; }
        if (m.owner_case==OWNER_WEAK_RSN) { p[at+19]=2; }
        if (m.owner_case==OWNER_DUP_RSN) { memcpy(p+at+m.ie_bytes,m.ie,m.ie_bytes); }
        if (m.owner_case==OWNER_BAD_PMKID) { p[at+1]+=16;p[at+22]=1;memcpy(p+at+40,m.ie+24,4);memset(p+at+24,0x55,16); }
        *actual=m.owner_case==OWNER_REQ_SHORT ? 2 : 512;
        if (m.captured_request) { captured(p);*actual=118; } /* no padding required by the new query */
        if (m.owner_case==OWNER_DEAUTH_IN_QUERY) { emit(6,1,0); }
    }
    else if (!strcmp(name,"auth_status")) {
        assert(m.association && set && bytes==60 && !capacity && !output && !m.status_sets++ && state.confirmed && client.phase==SAE_EXPORTED && (m.key_probe ? !assoc.keys_erased : assoc.keys_erased));
        unsigned char expected[60];assert(brcm_assoc_auth_status(&peer,ap.keys.pmkid,expected)==BLOCK_OK && !memcmp(input,expected,60));
        m.status_sent=true;m.assoc_polls=0;
        if (m.assoc_case==ASSOC_EARLY || m.assoc_case==ASSOC_STATUS_FAIL) {
            association_events();assert(assoc.ssid_event && assoc.m1_seen && !assoc.status_ack && !assoc.association_verified);
        }
        if (m.assoc_case==ASSOC_STATUS_FAIL) { *status=0xffffffe9u;return BLOCK_IO; }
    }
    else if (!strcmp(name,"join")) {
        assert(set && m.up && bytes==70 && !output && !capacity && !m.joins++);
        assert(m.values[0]==2 && m.values[1]==0x40000 && m.values[2]==0x604 && m.values[3]==2 && (m.mask[23]&0x78)==0x78);
        if (m.mode==EARLY || m.mode==SET_FAIL) {
            emit(188,0,1);emit(189,0,1);assert(!state.rx_seen && !state.tx_status_seen);
            emit(187,0,0);m.requested=true;
        }
        if (m.auth && m.auth_case==AUTH_EARLY_FLOOD) { emit(187,0,0);m.requested=true;for (unsigned i=0;i<256;++i) { emit(188,0,0); } }
    }else {
        assert(!strcmp(name,"mgmt_frame") && set && m.up && m.requested && !output && !capacity);
        const unsigned char *p=input;
        if (p[34]==2) {
            assert(m.auth && m.commit_txs && !m.confirm_txs++ && bytes==72 && get32(p+28)==m.last_cookie+1 && !p[36]);
            ++m.tx;m.last_cookie=get32(p+28);
            assert(!memcmp(p+38,client.confirm,34));
            enum sae_result accepted=sae_session_verify(&ap,p+38,34);
            assert(accepted==(m.auth_case==AUTH_WRONG_PWE ? SAE_CONFIRM : SAE_OK));
            m.tx_polls=0;
            if (m.auth_case==AUTH_EARLY_ALL || m.auth_case==AUTH_SET_FAIL) { confirm_events(); }
            if (m.auth_case==AUTH_SET_FAIL) { *status=0xffffffe9u;return BLOCK_IO; }
            return BLOCK_OK;
        }
        ++m.tx;
        assert(bytes==136 && !get32(p) && get32(p+4)==4000 && p[8]==104 && !p[9] && p[10]==0xb0 && p[12]==6);
        assert(!memcmp(p+14,peer.peer,6) && !memcmp(p+20,peer.peer,6) && get32(p+28)==1+m.commit_txs);
        if (m.commit_txs) { assert(m.auth && m.clock-m.last_commit_clock>=5000 && client.phase==SAE_COMMITTED); }
        ++m.commit_txs;m.last_commit_clock=m.clock;m.last_cookie=get32(p+28);m.tx_polls=0;
        assert(p[32]==3 && p[34]==1 && p[36]==(peer.hunting ? 0 : 126) && !memcmp(p+38,own,98));
        if (m.auth && m.commit_txs==1) { assert(sae_session_peer(&ap,own,98)==SAE_OK && sae_session_confirm(&ap,ap_confirm)==SAE_OK); }
        if (m.auth && m.auth_case==AUTH_RETRY_SET_FAIL && m.commit_txs==2) { events();*status=0xffffffe9u;return BLOCK_IO; }
        if (m.mode==EARLY || m.mode==SET_FAIL) { events();assert(state.tx_status_seen && state.rx_structural && !state.peer_valid); }
        if (m.mode==SET_FAIL) { *status=0xffffffe9u;return BLOCK_IO; }
    }
    return BLOCK_OK;
}
static enum block_result station(void *ctx,uint32_t *status) { assert(ctx==&m && !m.up);return touch(status); }
static enum block_result live(void *ctx,uint32_t *status) { assert(ctx==&m && m.up);return touch(status); }
static enum block_result radio(void *ctx,bool up,uint32_t *status)
{
    assert(ctx==&m);if (up) { ++m.ups; }else { ++m.downs;assert(state.confirm_mode || !state.peer_valid); }
    enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    if (!up && m.mode==DOWN_FAIL) { return BLOCK_IO; }m.up=up;
    if (up && m.association && m.assoc_case==ASSOC_IE_RESET_UP) { m.ie[20]^=1; }return BLOCK_OK;
}
static enum block_result poll(void *ctx)
{
    assert(ctx==&m && m.up);++m.polls;enum block_result r=touch(NULL);if (r!=BLOCK_OK) { return r; }
    if (m.status_sent) {
        if (++m.assoc_polls==1) { association_events(); }
        if (m.m2_txs && !m.m3_emitted && m.key_case!=KEY_NO_M3) { m3_event(); }
        if (m.install_case==15 && m.m4_txs && !m.retry_events) { retry_event(0); }
        return BLOCK_OK;
    }
    if (!m.requested) { emit(187,0,0);m.requested=true; }
    else if (m.tx) {
        ++m.tx_polls;
        if (m.auth && m.confirm_txs) { if (m.tx_polls==1) { confirm_events(); } }
        else if (m.mode==STORM) { for (unsigned i=0;i<257;++i) { emit(189,0,2); } }
        else if (m.tx_polls==1) {
            if (m.auth && m.auth_case==AUTH_COMMIT_NO_ACK) { emit(188,0,0);return BLOCK_OK; }
            if (m.auth && m.auth_case>=AUTH_COMMIT_LOST_ONCE &&
                (m.commit_txs==1 || m.auth_case==AUTH_COMMIT_NO_RX || m.auth_case==AUTH_COMMIT_STALLED || (m.auth_case==AUTH_COMMIT_LOST_TWICE && m.commit_txs==2))) {
                emit(189,0,m.last_cookie);return BLOCK_OK; /* reproduce capture: ACK but no RX/OC */
            }
            if (m.auth && m.auth_case==AUTH_RETRY_STALE_ACK) { emit(189,0,m.last_cookie-1);emit(188,0,0);return BLOCK_OK; }
            if (m.auth && m.auth_case==AUTH_RETRY_NACK) { emit(189,5,m.last_cookie);return BLOCK_OK; }
            if (m.auth && m.auth_case==AUTH_RETRY_IO) { return BLOCK_IO; }
            events();
            if (m.auth && m.auth_case==AUTH_EARLY_RX) { m.confirm_event=true;emit(188,0,1);m.confirm_event=false;assert(state.confirm_rx_seen); }
            if (m.auth && m.auth_case==AUTH_CHANGED_COMMIT) { remote[33]^=1;emit(188,0,1); }
        }
    }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ assert(ctx==&m && us==10000);enum block_result r=touch(NULL);if (m.tx && m.fail_delay) { return BLOCK_IO; }if (r==BLOCK_OK && m.mode!=STALLED && !(m.status_sent && m.assoc_case==ASSOC_STALLED)) { m.clock+=10; }return r; }
static uint64_t milliseconds(void *ctx) { assert(ctx==&m);if (m.auth && m.auth_case==AUTH_SLOW_MATH && state.phase==7) { m.clock+=5000; }return m.clock; }
static void poison(void *ctx,enum block_result r) { assert(ctx==&m && r!=BLOCK_OK);m.poisoned=true;++m.poisons; }
static const struct brcm_join_probe_io io={{&m,iovar},station,radio,poll,delay,milliseconds,poison,live,NULL,0};
static void seed(void)
{
    selected=&plain_state;memset(&m,0,sizeof(m));memset(&state,0,sizeof(state));m.values[2]=0x200;m.values[3]=1;m.mask[0]=0xa5;m.mask[23]=0x80;
    memset(own,0,sizeof(own));own[0]=19;own[33]=5;p256_generator(own+34);memcpy(remote,own,98);remote[33]=7;
}
static void restored(void)
{ assert(state.join.policy.restored && !m.up && !m.values[0] && !m.values[1] && m.values[2]==0x200 && m.values[3]==1 && m.mask[0]==0xa5 && m.mask[23]==0x80); }
static void erased(void)
{
    assert(state.erased && !state.join.armed && !state.session && !state.handoff);for (unsigned i=0;i<98;++i) { assert(!state.own_commit[i] && !state.peer_commit[i]); }
    for (unsigned i=0;i<34;++i) { assert(!state.own_confirm[i] && !state.peer_confirm[i]); }
    if (m.auth) { const unsigned char *p=(const void *)&client;for (size_t i=0;i<sizeof(client);++i) { assert(!p[i]); } }
}
static enum block_result run(void) { return brcm_sae_probe_run(&io,&peer,own,1,true,&state); }
static bool public_random(void *ctx,unsigned char out[32])
{ unsigned *v=ctx;memset(out,0,32);out[31]=(unsigned char)(*v)++;return true; }
static void auth_seed(enum auth_case mode)
{
    seed();m.auth=true;m.auth_case=mode;m.mode=METADATA_V1;sae_session_clear(&client);sae_session_clear(&ap);
    if (mode==AUTH_COMMIT_STALLED) { m.mode=STALLED; }
    unsigned char pwe[64],ap_pwe[64];p256_generator(pwe);
    if (peer.hunting) { assert(sae_hunt_pwe("public model fixture",20,peer.local,peer.peer,pwe)); }memcpy(ap_pwe,pwe,64);
    if (mode==AUTH_WRONG_PWE) { assert(p256_negate(pwe,ap_pwe)); }
    unsigned a=2,b=4;assert(sae_session_start(&client,pwe,public_random,&a)==SAE_OK && sae_session_start(&ap,ap_pwe,public_random,&b)==SAE_OK);
    assert(sae_session_commit(&client,own)==SAE_OK && sae_session_commit(&ap,remote)==SAE_OK);
}
static enum block_result auth_run(void) { return brcm_sae_auth_run(&io,&peer,&client,1,true,&state); }
static enum block_result link_identity(void *ctx,struct brcm_link_identity *out,uint32_t *status)
{
    assert(ctx==&m && m.up && !m.in_callback && m.status_sent && state.confirmed);enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    *out=(struct brcm_link_identity){.ssid_length=peer.ssid_length};memcpy(out->bssid,peer.peer,6);memcpy(out->ssid,peer.ssid,peer.ssid_length);
    if (m.install_probe && assoc.identity_queries==2 && !m.retry_events && (m.install_case==11 || (m.install_case>=16 && m.install_case<=19))) {
        unsigned fault=m.install_case==16 ? 1 : m.install_case==17 ? 2 : m.install_case==18 ? 3 : m.install_case==19 ? 5 : 0;retry_event(fault);
    }
    if (m.assoc_case==ASSOC_WRONG_ID) { out->bssid[5]^=1; }
    if (m.assoc_case==ASSOC_ZERO_ID && assoc.identity_queries<3) { memset(out->bssid,0,6); }
    if (m.assoc_case==ASSOC_DOWNGRADE) { m.values[1]|=0x80; }return BLOCK_OK;
}
static void assoc_seed(enum assoc_case mode)
{
    auth_seed(mode==ASSOC_BAD_SAE ? AUTH_BAD_HMAC : AUTH_NORMAL);selected=&assoc.sae;memset(&assoc,0,sizeof(assoc));m.association=true;m.assoc_case=mode;
    if (mode==ASSOC_OLD_IE) { assert(brcm_assoc_rsn(m.ie,256,&m.ie_bytes)==BLOCK_OK);m.ie[20]=0x80;memcpy(m.original_ie,m.ie,m.ie_bytes);m.original_bytes=m.ie_bytes; }
}
static bool key_random(void *ctx,unsigned char out[32])
{
    assert(ctx==&m && !m.in_callback && state.confirmed);
    if (assoc.status_issued) {
        assert(m.dhcp_probe && assoc.post_m4_identity && assoc.keys.installed && assoc.m4_written);
        if (m.gateway_probe && assoc.dhcp.phase==DHCP_ACKED) { for (unsigned i=0;i<32;++i) { out[i]=(unsigned char)(i+1); }return m.gateway_case!=9; }
        memset(out,0,32);out[0]=0x12;out[1]=0x34;out[2]=0x56;out[3]=0x78;return m.dhcp_case!=5;
    }
    memset(out,0x37,32);return m.key_case!=KEY_RNG_FAIL;
}
static void wrap_public(const unsigned char key[16],const unsigned char *plain,size_t n,unsigned char *out)
{
    memset(out,0xa6,8);memcpy(out+8,plain,n);unsigned blocks=(unsigned)n/8;
    for (unsigned j=0;j<6;++j) { for (unsigned i=1;i<=blocks;++i) {
        unsigned char b[16];memcpy(b,out,8);memcpy(b+8,out+8*i,8);assert(aes128_encrypt(key,b,b));
        memcpy(out,b,8);unsigned t=blocks*j+i;out[6]^=(unsigned char)(t>>8);out[7]^=(unsigned char)t;memcpy(out+8*i,b+8,8);
    } }
}
static enum block_result key_tx(void *ctx,const void *frame,size_t n)
{
    assert(ctx==&m && !m.in_callback && m.up && assoc.association_verified && assoc.assoc_ie_verified && assoc.status_ack && assoc.m1_seen && !assoc.m2_written && !m.m2_txs++);
    enum block_result result=touch(NULL);if (result!=BLOCK_OK) { return result; }
    const unsigned char *p=frame;assert(n>=135 && !memcmp(p,peer.peer,6) && !memcmp(p+6,peer.local,6) && p[19]==1 && p[20]==8);
    unsigned char an[32],mic[16],copy[370];memset(an,0x42,32);struct wpa_sae_ptk ptk;
    assert(wpa_sae_ptk(ap.keys.pmk,peer.local,peer.peer,p+31,an,&ptk));memcpy(copy,p,n);memset(copy+95,0,16);
    memcpy(m.expected_tk,ptk.tk,16);
    memcpy(m.expected_kck,ptk.kck,16);memcpy(m.expected_kek,ptk.kek,16);
    assert(aes128_cmac(ptk.kck,copy+14,n-14,mic) && crypto_equal(mic,p+95,16));
    memset(m.m3,0,sizeof(m.m3));m.m3[0]=0x20;memcpy(m.m3+4,peer.local,6);memcpy(m.m3+10,peer.peer,6);be16(m.m3+16,0x888e);
    unsigned char plain[128]={0};size_t bytes=16;for (unsigned i=0;i<16;++i) { plain[i]=(unsigned char)i; }
    if (m.key_data_probe) {
        memset(plain,0,sizeof(plain));memcpy(plain,ap_rsn,sizeof(ap_rsn));
        const unsigned char gtk[]={221,22,0,15,172,1,1,0},igtk[]={221,28,0,15,172,9,4,0};memcpy(plain+26,gtk,8);memcpy(plain+50,igtk,8);
        for (unsigned i=0;i<16;++i) { plain[34+i]=(unsigned char)i;plain[64+i]=(unsigned char)(i+16); }bytes=80;
        if (m.kde_mode==1) { plain[4]^=1; }
        if (m.kde_mode==2) { memcpy(plain+bytes,ap_rsn,26);bytes+=26; }
        if (m.kde_mode==3) { plain[32]|=128; }
        if (m.kde_mode==4) { plain[56]=6; }
        if (m.kde_mode==5) { bytes=50; }
        size_t pad=(8-bytes%8)%8;if (pad) { plain[bytes]=221;bytes+=pad; }
    }
    unsigned char *e=m.m3+18;e[0]=2;e[1]=3;be16(e+2,(unsigned)(103+bytes));e[4]=2;be16(e+5,0x13c8);be16(e+7,16);e[16]=2;memset(e+17,0x42,32);be16(e+97,(unsigned)(8+bytes));wrap_public(ptk.kek,plain,bytes,e+99);
    if (m.key_case==KEY_BAD_WRAP) { e[99]^=1; }
    assert(aes128_cmac(ptk.kck,e,107+bytes,e+81));m.m3_bytes=125+bytes;crypto_wipe(&ptk,sizeof(ptk));
    if (m.key_case==KEY_BAD_MIC) { e[81]^=1; }
    if (m.key_case==KEY_REPLAY) { e[16]=1; }
    if (m.key_case==KEY_NONCE) { e[17]^=1; }
    if (m.key_case==KEY_PROFILE) { e[6]^=2; }
    if (m.key_case==KEY_EARLY || m.key_case==KEY_WRITE_FAIL) { m3_event();assert(assoc.m3_bytes==m.m3_bytes-18 && !assoc.m3_mic_verified && !assoc.m2_written); }
    return m.key_case==KEY_WRITE_FAIL ? BLOCK_IO : BLOCK_OK;
}
static enum block_result assoc_run(void)
;
static enum block_result key_install_set(void *ctx,const void *wire,size_t n,uint32_t *status)
{
    assert(ctx==&m && !m.in_callback && brcm_key_wire_valid(wire,n));const unsigned char *p=wire;bool remove=!get32(p+4);
    enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    unsigned index=get32(p),kind=get32(p+116)==0 ? 0 : index<=3 ? 1 : 2;
    if (remove) {
        assert(m.downs && assoc.keys.cleanup_active);++m.clear_sets;
        if (m.install_case==6 && kind==1) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }return BLOCK_OK;
    }
    assert(m.up && assoc.key_data_verified && assoc.m3_mic_verified && assoc.m3_unwrap_verified && assoc.m4_written && assoc.first_m4_before_keys && m.key_sets==kind);++m.key_sets;
    if (!kind) { assert(!memcmp(p+8,m.expected_tk,16) && !memcmp(p+156,peer.peer,6)); }
    else { for (unsigned i=0;i<16;++i) { assert(p[8+i]==i+(kind==2 ? 16u : 0u)); } }
    if (m.install_case==kind+1) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
    if (m.install_case==5 && kind==1) { emit(6,1,0); }
    if (m.install_case==9 && kind==1) { m3_event(); }
    if (m.install_case==10 && kind==2) { m.values[1]|=0x80; }
    if (m.install_case==12 && kind==1) { retry_event(0); }
    if (m.install_case==20 && kind==1) { for (unsigned i=0;i<9;++i) { m3_event(); } }
    return BLOCK_OK;
}
static enum block_result m4_tx(void *ctx,const void *frame,size_t n)
{
    assert(ctx==&m && !m.in_callback && n==113 && ((!assoc.keys.attempted && !m.key_sets) || (assoc.keys.installed && assoc.keys.ack_mask==7)) && m.m4_txs<4);++m.m4_calls;
    enum block_result r=touch(NULL);if (r!=BLOCK_OK) { return r; }
    if ((m.install_case==13 && m.m4_calls==1) || m.install_case==21) { retry_event(0);return BLOCK_NOT_READY; }
    const unsigned char *p=frame;assert(!memcmp(p,peer.peer,6) && !memcmp(p+6,peer.local,6) && p[19]==3 && p[20]==8 && !p[112]);
    assert(!memcmp(p+23,m.m3+18+9,8));++m.m4_txs;
    unsigned char an[32],nonce[32],copy[113],mic[16];struct wpa_sae_ptk ptk;memset(an,0x42,32);memset(nonce,0x37,32);
    assert(wpa_sae_ptk(ap.keys.pmk,peer.local,peer.peer,nonce,an,&ptk));memcpy(copy,p,n);memset(copy+95,0,16);assert(aes128_cmac(ptk.kck,copy+14,99,mic) && !memcmp(mic,p+95,16));crypto_wipe(&ptk,sizeof(ptk));
    if (m.install_case==4) { return BLOCK_IO; }if (m.install_case==7) { emit(6,1,0); }if (m.install_case==8) { m.assoc_case=ASSOC_WRONG_ID; }
    if (m.install_case==14 && m.m4_txs==1) { retry_event(0); }if (m.install_case==22) { m3_event(); }
    return BLOCK_OK;
}
static enum block_result dhcp_tx(void *ctx,const void *frame,size_t n)
{
    assert(ctx==&m && !m.in_callback && m.up && m.dhcp_probe && m.authorized && assoc.port_verified && assoc.post_m4_identity && assoc.keys.installed && assoc.m4_written && dhcp_wire_request_valid(frame,n));
    ++m.dhcp_calls;enum block_result r=touch(NULL);if (r!=BLOCK_OK) { return r; }
    if (((const unsigned char *)frame)[284]==4) { assert(assoc.decline_active && assoc.network.conflict && !assoc.network.active && !assoc.network.armed);++m.declines;return m.gateway_case==10 ? BLOCK_IO : BLOCK_OK; }
    if (m.dhcp_case==6 && m.dhcp_calls==1) { retry_event(0);return BLOCK_NOT_READY; }
    if (m.dhcp_case==7) { emit(6,1,0);return BLOCK_IO; }
    ++m.dhcp_txs;const unsigned char *p=frame;bool request=p[284]==3;
    if (m.dhcp_case==1 || (m.dhcp_case==2 && request)) { return BLOCK_OK; }
    unsigned char bcdc[346]={0x20};memcpy(bcdc+4,request ? dhcp_ack_fixture : dhcp_offer_fixture,342);
    if (request && m.gateway_probe && (m.gateway_case==5 || m.gateway_case==13)) {
        for (unsigned at=286;at<sizeof(bcdc);) {
            unsigned tag=bcdc[at++];if (!tag) { continue; }if (tag==255) { break; }unsigned size=bcdc[at++];
            if (tag==3 && m.gateway_case==5) { bcdc[at-2]=222; }
            if (tag==51 && m.gateway_case==13) { memset(bcdc+at,0,4);bcdc[at+3]=1; }at+=size;
        }
    }
    if (m.dhcp_case==3 && request) {
        for (unsigned at=4+282;at<sizeof(bcdc);) {
            unsigned tag=bcdc[at++];if (!tag) { continue; }if (tag==255) { break; }unsigned bytes=bcdc[at++];
            if (tag==53) { bcdc[at]=6;break; }at+=bytes;
        }
    } /* selected-server NAK; fixture has legal zero IPv4 UDP checksum */
    m.in_callback=true;brcm_assoc_data(&assoc,bcdc,sizeof(bcdc));m.in_callback=false;
    if (m.dhcp_case==4 && request) { return BLOCK_IO; }
    if (m.dhcp_case==8 && request) { m.assoc_case=ASSOC_WRONG_ID; }
    return BLOCK_OK;
}
static enum block_result peer_info(void *ctx,const unsigned char mac[6],struct brcm_station_info *out,uint32_t *status)
{
    assert(ctx==&m && !m.in_callback && m.up && assoc.post_m4_identity && assoc.keys.installed && !memcmp(mac,peer.peer,6));++m.peer_queries;
    enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    *out=(struct brcm_station_info){.version=7,.length=280,.flags=m.authorized ? 0x4038 : 0x4018,.valid=true,.statistics=true,.tx_packets=m.dhcp_txs,.rx_unicast=m.dhcp_txs};memcpy(out->peer,mac,6);
    if (m.port_case==2) { out->valid=false;return BLOCK_UNSUPPORTED; }
    if (m.port_case==3) { out->peer[5]^=1; }
    if (m.port_case==4 || (m.port_case==8 && m.dhcp_txs)) { out->flags&=~0x20u; }
    if (m.port_case==6 && m.peer_queries==1) { retry_event(1); }
    if (m.port_case==7) { out->flags&=~8u; }
    if (m.port_case==9 && m.peer_queries==1) { return BLOCK_IO; }
    if (m.port_case==10 && m.dhcp_txs) { return BLOCK_IO; }
    return BLOCK_OK;
}
static enum block_result port_set(void *ctx,const unsigned char mac[6],bool authorize,uint32_t *status)
{
    assert(ctx==&m && !m.in_callback && m.up && !memcmp(mac,peer.peer,6));enum block_result r=touch(status);if (r!=BLOCK_OK) { return r; }
    if (!authorize) {
        assert(assoc.port_close_active && assoc.port_issued);++m.port_closes;if (m.port_case==5) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }m.authorized=false;return BLOCK_OK;
    }
    assert(assoc.keys.installed && assoc.m4_written && assoc.post_m4_identity && !assoc.dhcp.attempted);++m.port_opens;
    if (m.port_case==1) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
    if (m.port_case==11 && m.port_opens==1) { retry_event(0);return BLOCK_NOT_READY; }
    m.authorized=true;return BLOCK_OK;
}
static unsigned inet_sum(const unsigned char *p,size_t n)
{ unsigned s=0;for (size_t i=0;i<n;i+=2) { s+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }while (s>>16) { s=(s&65535)+(s>>16); }return (~s)&65535; }
static void gateway_receive(const void *frame,size_t n)
{ unsigned char bcdc[78]={0x20};assert(n<=74);memcpy(bcdc+4,frame,n);m.in_callback=true;brcm_assoc_data(&assoc,bcdc,n+4);m.in_callback=false; }
static enum block_result net_tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *frame,size_t n)
{
    assert(ctx==&m && !m.in_callback && m.gateway_probe && m.authorized && assoc.port_verified && assoc.dhcp.phase==DHCP_ACKED && netprobe_packet_valid(&assoc.network,kind,sequence,frame,n) && netprobe_can_tx(&assoc.network,m.clock));
    enum block_result r=touch(NULL);if (r!=BLOCK_OK) { return r; }++m.net_calls;
    unsigned point=kind==NP_ECHO ? 4+sequence : (unsigned)kind+1;if (!m.net_fail_points[point]) { m.net_fail_points[point]=m.calls; }
    if (m.gateway_case==4 && m.net_calls==1) { retry_event(0);return BLOCK_NOT_READY; }
    if (m.gateway_case==6) { return BLOCK_IO; }if (m.gateway_case==8) { emit(6,1,0);return BLOCK_IO; }++m.net_writes;
    if ((kind==NP_ARP_PROBE && (m.gateway_case==1 || m.gateway_case==10)) || (kind==NP_ARP_ANNOUNCE && assoc.network.active && m.gateway_case==11)) {
        unsigned char conflict[60];memcpy(conflict,np_gateway_fixture,60);memcpy(conflict+28,assoc.dhcp.lease.address,4);gateway_receive(conflict,60);assert(assoc.network.conflict);return BLOCK_OK;
    }
    if (kind==NP_ARP_RESOLVE && m.gateway_case!=2) { gateway_receive(np_gateway_fixture,60);assert(assoc.network.gateway_known); }
    if (kind==NP_ECHO && m.gateway_case!=3) {
        unsigned char reply[74];memcpy(reply,np_echo_fixture,74);be16(reply+40,sequence);reply[36]=reply[37]=0;be16(reply+36,inet_sum(reply+34,40));
        if (m.gateway_case==12) { reply[36]^=1; }gateway_receive(reply,74);
        if (m.gateway_case==7) { assert(assoc.network.seen_mask && !assoc.network.written_mask);return BLOCK_IO; }
    }
    return BLOCK_OK;
}
static enum block_result assoc_run(void)
{ const struct brcm_assoc_io aio={.join=io,.identity=link_identity,.fresh_ie_owner=m.ie_owner,.request_fixed_bytes=m.fixed_request ? 4 : 0,.key_probe=m.key_probe,.data_tx=key_tx,.random=key_random,.key_data_probe=m.key_data_probe,.ap_rsn=ap_rsn,.ap_rsn_bytes=sizeof(ap_rsn),.install_probe=m.install_probe,.key_set=key_install_set,.m4_tx=m4_tx,.dhcp_probe=m.dhcp_probe,.dhcp_tx=dhcp_tx,.peer_info=peer_info,.port_set=port_set,.gateway_probe=m.gateway_probe,.net_tx=net_tx};return brcm_assoc_run(&aio,&peer,&client,true,&assoc); }
static void leaf_checks(void)
{
    unsigned char event[76]={0};event[0]=0x20;unsigned char *wire=event+4;
    be16(wire+12,0x886c);be16(wire+14,0x8001);wire[20]=16;wire[21]=24;be16(wire+22,1);be16(wire+24,2);
    memcpy(wire+48,peer.peer,6);be32(wire+44,1); /* declared byte is missing */
    struct brcm_assoc_probe malformed={.peer=peer,.armed=true,.status_issued=true};
    malformed.sae.confirmed=true;malformed.sae.handoff_attempted=true;
    brcm_assoc_event(&malformed,event,sizeof(event));assert(!malformed.ssid_event && malformed.ignored==1);
    be32(wire+44,0);brcm_assoc_event(&malformed,event,sizeof(event));assert(malformed.ssid_event);
    unsigned char rsn[30],pmkid[16],status[62];size_t n=99;memset(rsn,0xa5,sizeof(rsn));memset(pmkid,0x5a,16);memset(status,0xa5,sizeof(status));
    assert(brcm_assoc_rsn(rsn,27,&n)==BLOCK_RANGE && !n && rsn[0]==0xa5);
    assert(brcm_assoc_rsn(rsn,28,&n)==BLOCK_OK && n==28 && rsn[0]==0x30 && rsn[1]==26 && rsn[19]==8 && rsn[20]==0xc0 && rsn[27]==6 && rsn[28]==0xa5);
    assert(brcm_assoc_auth_status(&peer,pmkid,status)==BLOCK_OK && status[0]==4 && !status[1] && !memcmp(status+2,peer.peer,6) && get32(status+8)==12 && !memcmp(status+12,peer.ssid,12) && !memcmp(status+44,pmkid,16) && status[60]==0xa5);
    for (unsigned i=24;i<44;++i) { assert(!status[i]); } /* no uninitialized SSID tail */
    unsigned char ies[128]={0};ies[1]=peer.ssid_length;memcpy(ies+2,peer.ssid,peer.ssid_length);size_t base=2+peer.ssid_length;
    memcpy(ies+base,rsn,28);bool h2e=true;
    for (size_t cut=0;cut<base+28;++cut) { assert(brcm_assoc_request_ies(&peer,pmkid,ies,cut,&h2e)!=BLOCK_OK); }
    assert(brcm_assoc_request_ies(&peer,pmkid,ies,base+28,&h2e)==BLOCK_OK && !h2e);
    struct brcm_assoc_probe diagnostics={0};
    assert(brcm_assoc_request_check(&peer,pmkid,ies+base,28,&h2e,&diagnostics)==BLOCK_UNSUPPORTED && diagnostics.request_error==7);
    ies[base+1]=20;assert(brcm_assoc_request_check(&peer,pmkid,ies,base+22,&h2e,&diagnostics)==BLOCK_OK && !diagnostics.request_rsn.management_explicit);
    ies[base+20]=0x80;assert(brcm_assoc_request_check(&peer,pmkid,ies,base+22,&h2e,&diagnostics)==BLOCK_UNSUPPORTED && diagnostics.request_error==4 && diagnostics.request_rsn_error==RSN_PMF);
    ies[base+20]=0xc0;ies[base+1]=26;
    ies[base+28]=244;ies[base+29]=1;ies[base+30]=32;
    assert(brcm_assoc_request_ies(&peer,pmkid,ies,base+31,&h2e)==BLOCK_OK && h2e);
    ies[base+30]=0;assert(brcm_assoc_request_ies(&peer,pmkid,ies,base+31,&h2e)==BLOCK_UNSUPPORTED);
    ies[base+1]=42;ies[base+22]=1;memcpy(ies+base+24,pmkid,16);memcpy(ies+base+40,rsn+24,4);
    assert(brcm_assoc_request_ies(&peer,pmkid,ies,base+44,&h2e)==BLOCK_OK);
    ies[base+24]^=1;assert(brcm_assoc_request_ies(&peer,pmkid,ies,base+44,&h2e)==BLOCK_UNSUPPORTED);
    uint32_t ie_random=0x51a;unsigned char mutated[128];
    for (unsigned i=0;i<10000;++i) {
        struct { unsigned char before[8];bool value;unsigned char after[8]; } output;memset(&output,0xa5,sizeof(output));
        memset(mutated,0,sizeof(mutated));mutated[1]=peer.ssid_length;memcpy(mutated+2,peer.ssid,peer.ssid_length);memcpy(mutated+base,rsn,28);
        ie_random=ie_random*1664525u+1013904223u;mutated[ie_random%128]^=(unsigned char)(ie_random>>24);
        (void)brcm_assoc_request_ies(&peer,pmkid,mutated,i%3 ? base+28 : ie_random%129,&output.value);
        for (unsigned j=0;j<8;++j) { assert(output.before[j]==0xa5 && output.after[j]==0xa5); }
    }
    puts("PASS: association-request SSID/RSN/PMKID/RSNXE parsing, all required-prefix truncations, 10000 mutations and output guards");
    unsigned char original[117]={0x20};memcpy(original+4,peer.local,6);memcpy(original+10,peer.peer,6);be16(original+16,0x888e);
    original[18]=2;original[19]=3;be16(original+20,95);original[22]=2;be16(original+23,0x88);be16(original+25,16);memset(original+35,0x42,32);
    struct { struct brcm_assoc_probe s;unsigned char guard[16]; } h;
    for (unsigned cut=0;cut<117;++cut) {
        memset(&h,0,sizeof(h));h.s.peer=peer;h.s.armed=true;h.s.status_issued=true;h.s.sae.confirmed=true;h.s.sae.handoff_attempted=true;
        brcm_assoc_data(&h.s,original,cut);assert(!h.s.m1_seen);
    }
    h.s=(struct brcm_assoc_probe){.peer=peer,.armed=true,.status_issued=true};
    brcm_assoc_data(&h.s,original,117);assert(!h.s.data_frames && !h.s.m1_seen); /* cannot run before SAE verifies */
    unsigned char frame[117];uint32_t random=0x91;
    for (unsigned i=0;i<10000;++i) {
        memset(&h,0,sizeof(h));memset(h.guard,0xa5,16);h.s.peer=peer;h.s.armed=true;h.s.status_issued=true;h.s.sae.confirmed=true;h.s.sae.handoff_attempted=true;
        memcpy(frame,original,117);random=random*1664525u+1013904223u;frame[random%117]^=(unsigned char)(random>>24);
        brcm_assoc_data(&h.s,frame,i%7 ? 117 : random%118);
        assert(!h.s.association_verified && !h.s.identity_verified && !h.s.status_ack && !h.s.keys_erased);
        for (unsigned j=0;j<16;++j) { assert(h.guard[j]==0xa5); }
    }
    puts("PASS: byte-exact RSN/zero-padded PMKID serializers, BCDC/EAPOL truncation and 10000 mutations, before-SAE gating, output bounds and no port authorization from unauthenticated M1");
}
int main(void)
{
    leaf_checks();
    seed();assert(brcm_sae_probe_run(&io,&peer,own,1,false,&state)==BLOCK_NOT_READY && !m.calls && !state.attempted);
    own[33]=1;assert(run()==BLOCK_ARGUMENT && !m.calls && !state.attempted);
    seed();own[97]^=1;assert(run()==BLOCK_ARGUMENT && !m.calls && !state.attempted);
    seed();assert(brcm_sae_probe_run(&io,&peer,own,0,true,&state)==BLOCK_ARGUMENT && !m.calls);
    seed();assert(run()==BLOCK_OK && state.phase==6 && state.peer_valid && state.tx_set_acknowledged && state.tx_status_seen && !state.tx_status);
    assert(state.join.request_seen && state.join.policy.external_request_verified && m.tx==1 && m.ups==1 && m.downs==1);restored();erased();
    unsigned total=m.calls;assert(run()==BLOCK_OK && m.calls==total);
    unsigned events_before=state.events;emit(188,0,1);emit(189,5,1);assert(state.events==events_before);erased();
    for (enum mode mode=EARLY;mode<MODE_COUNT;mode++) {
        seed();m.mode=mode;if (mode==BAD_POINT) { remote[97]^=1; }if (mode==REFLECTION) { memcpy(remote,own,98); }
        enum block_result r=run();
        if (mode==EARLY || mode==RX_FIRST || mode==DUPLICATE || mode==METADATA_V1) { assert(r==BLOCK_OK && state.peer_valid); }
        else {
            assert(r!=BLOCK_OK && !state.peer_valid);
            if (mode==REFLECTION) { assert(state.reflection && r==BLOCK_RANGE); }
            if (mode==SET_FAIL) { assert(state.tx_status_seen && state.rx_structural && !state.tx_set_acknowledged && r==BLOCK_IO); }
            if (mode==NACK) { assert(state.tx_status==5 && r==BLOCK_IO); }
            if (mode==NO_ACK || mode==NO_RX || mode==OFF_ONLY || mode==STALLED || mode==WRONG_PEER || mode==WRONG_CHANNEL || mode==WRONG_COOKIE) { assert(r==BLOCK_TIMEOUT && m.tx_polls<=500); }
            if (mode==OFF_ONLY) { assert(state.offchannel_seen && !state.tx_status_seen && !state.rx_seen); }
            if (mode==TOKEN_NEEDED) { assert(state.auth_status==76 && state.wire_body_bytes==98); }
            if (mode==EXTENDED) { assert(state.wire_body_bytes==99 && !state.rx_structural); }
            if (mode==NEW_METADATA) { assert(state.metadata_version==2 && r==BLOCK_UNSUPPORTED); }
            if (mode==WRONG_PEER || mode==WRONG_CHANNEL) { assert(state.rx_filtered==1 && !state.rx_seen && state.wire_auth_rx==1); }
            if (mode==STORM) { assert(state.events==257 && r==BLOCK_RANGE); }
        }
        if (mode==EARLY) { assert(state.rx_early==1 && state.early_events==3 && state.trace_count>3); }
        if (mode==STORM) { for (unsigned i=0;i<BRCM_SAE_TRACE_MAX;++i) { assert(state.trace[i].event==189 && state.trace[i].filter==AUTH_FILTER_COOKIE); } }
        erased();if (mode!=DOWN_FAIL) { restored(); }else { assert(m.poisoned && !state.join.down && !state.join.policy.restored); }
        unsigned calls=m.calls;(void)run();assert(m.calls==calls && m.tx==1);
    }
    /* Fail every operation of a successful flow: restoration must verify or
     * poison I/O; once UP is attempted there must always be one DOWN attempt. */
    for (unsigned fail=1;fail<=total;++fail) {
        seed();m.fail_at=fail;assert(run()!=BLOCK_OK);erased();
        if (m.ups) { assert(m.downs==1); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
        unsigned calls=m.calls;(void)run();assert(m.calls==calls);
    }
    seed();m.mode=NO_ACK;m.fail_delay=true;assert(run()==BLOCK_IO && state.tx_attempted);restored();erased();
    printf("PASS: one-commit SAE transport, early/out-of-order RX/ACK, NACK/SET failure, cookie/peer/channel binding, peer validation/reflection, bounded/stalled timeouts, event flood, %u operation failures, DOWN/restore/poisoning and erasure\n",total);
    auth_seed(AUTH_NORMAL);assert(auth_run()==BLOCK_OK && state.phase==10 && state.confirmed && state.confirm_set_acknowledged && state.confirm_tx_status_seen && !state.confirm_tx_status && state.confirm_rx_seen && m.tx==2);restored();erased();unsigned auth_total=m.calls;
    for (enum auth_case mode=AUTH_EARLY_RX;mode<AUTH_COUNT;mode++) {
        auth_seed(mode);enum block_result r=auth_run();
        if (mode==AUTH_EARLY_RX || mode==AUTH_EARLY_ALL || mode==AUTH_COMMIT_LOST_ONCE || mode==AUTH_COMMIT_LOST_TWICE) {
            assert(r==BLOCK_OK && state.confirmed);
            if (mode==AUTH_COMMIT_LOST_ONCE || mode==AUTH_COMMIT_LOST_TWICE) {
                unsigned tries=mode==AUTH_COMMIT_LOST_ONCE ? 2 : 3;assert(state.commit_attempts==tries && m.commit_txs==tries && m.confirm_txs==1 && state.active_packet_id==tries+1);
            }
        }
        else {
            assert(r!=BLOCK_OK && !state.confirmed);
            if (mode==AUTH_BAD_HMAC || mode==AUTH_WRONG_PWE) { assert(state.crypto_result==SAE_CONFIRM && state.confirm_tx_status_seen); }
            if (mode==AUTH_SLOW_MATH) { assert(state.key_math_ms==5000 && !state.confirm_tx_attempted && r==BLOCK_TIMEOUT); }
            if (mode==AUTH_SET_FAIL) { assert(state.confirm_tx_status_seen && state.confirm_rx_seen && !state.confirm_set_acknowledged); }
            if (mode==AUTH_COMMIT_NO_RX) { assert(r==BLOCK_TIMEOUT && state.commit_attempts==3 && !state.confirm_tx_attempted && !state.crypto_attempted && m.clock==15000 && state.trace_count>=4); }
            if (mode==AUTH_COMMIT_NO_ACK || mode==AUTH_COMMIT_STALLED) { assert(r==BLOCK_TIMEOUT && state.commit_attempts==1 && !state.confirm_tx_attempted); }
            if (mode==AUTH_RETRY_SET_FAIL) { assert(state.commit_attempts==2 && state.rx_structural && !state.tx_set_acknowledged && !state.confirm_tx_attempted); }
            if (mode==AUTH_RETRY_STALE_ACK) { assert(state.commit_attempts==2 && !state.tx_status_seen && !state.confirm_tx_attempted); }
            if (mode==AUTH_EARLY_FLOOD) { assert(r==BLOCK_RANGE && !m.tx && state.early_events==257 && state.trace_count==256); }
        }
        restored();erased();unsigned calls=m.calls;(void)auth_run();assert(m.calls==calls);
    }
    for (unsigned fail=1;fail<=auth_total;++fail) {
        auth_seed(AUTH_NORMAL);m.fail_at=fail;assert(auth_run()!=BLOCK_OK);erased();
        if (m.ups) { assert(m.downs==1); }if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    auth_seed(AUTH_NORMAL);m.mode=DOWN_FAIL;assert(auth_run()!=BLOCK_OK && m.poisoned && !state.join.down);erased();
    auth_seed(AUTH_NORMAL);assert(brcm_sae_auth_run(&io,&peer,&client,UINT32_MAX-2,true,&state)==BLOCK_ARGUMENT && !m.calls);sae_session_clear(&client);
    /* Trace retains only a fixed ring; filtered envelopes never acquire kind
     * or body trust. Late events after DOWN cannot change it or revive keys. */
    auth_seed(AUTH_COMMIT_NO_RX);assert(auth_run()==BLOCK_TIMEOUT);unsigned count=state.trace_count;emit(188,0,0);assert(state.trace_count==count);erased();
    printf("PASS: live SAE commit/confirm state machine, RX metadata v1, early confirm/ACK, mismatched PWE/bad HMAC, stale cookies, NACK/SET failures, slow math refusal, %u operation failures, DOWN/restore and full private-session erasure; no key export or auth-success command\n",auth_total);
    puts("PASS: captured ACK/no-RX replay, immutable commit/session retransmission after 5 seconds, distinct cookies, second/third-attempt recovery, three-attempt cap, no retry on malformed RX/NACK/missing ACK/stalled clock/I/O, early/filtered RX diagnostics and cookie bounds");
    assoc_seed(ASSOC_NORMAL);assert(assoc_run()==BLOCK_OK && state.phase==12 && state.confirmed && state.handoff_attempted && assoc.status_ack && assoc.identity_verified && assoc.policy_verified && assoc.association_verified && assoc.m1_seen && assoc.keys_erased && assoc.ie_restored);restored();erased();unsigned assoc_total=m.calls;
    for (enum assoc_case mode=ASSOC_EARLY;mode<ASSOC_COUNT;mode++) {
        assoc_seed(mode);enum block_result r=assoc_run();
        if (mode==ASSOC_EARLY || mode==ASSOC_ZERO_ID || mode==ASSOC_OLD_IE || mode==ASSOC_DATA_PADDING) { assert(r==BLOCK_OK && assoc.association_verified && assoc.m1_seen); }
        else {
            assert(r!=BLOCK_OK);
            if (mode==ASSOC_NO_M1) { assert(assoc.association_verified && !assoc.m1_seen && r==BLOCK_TIMEOUT); }
            if (mode==ASSOC_NO_EVENT || mode==ASSOC_WRONG_PEER || mode==ASSOC_WRONG_IF) { assert(!assoc.association_verified && !assoc.identity_queries && r==BLOCK_TIMEOUT); }
            if (mode==ASSOC_BAD_SAE) { assert(!assoc.status_issued && !m.status_sets && !assoc.keys_erased); }
            if (mode==ASSOC_STATUS_FAIL) { assert(assoc.m1_seen && !assoc.status_ack && !assoc.association_verified); }
            if (mode==ASSOC_IE_IGNORE || mode==ASSOC_IE_BAD_SNAPSHOT) { assert(!m.ups && !m.status_sets); }
            if (mode==ASSOC_REJECT || mode==ASSOC_DEAUTH) { assert(assoc.failure_status==1 && assoc.failure_reason==17); }
            if (mode==ASSOC_IE_RESTORE_FAIL) { assert(m.poisoned && !assoc.ie_restored && state.join.down && state.join.policy.restored); }
        }
        erased();restored();assert(!assoc.armed);
        if (mode!=ASSOC_IE_RESTORE_FAIL) { assert(m.ie_bytes==m.original_bytes && !memcmp(m.ie,m.original_ie,m.ie_bytes)); }
        unsigned calls=m.calls;(void)assoc_run();assert(m.calls==calls);
    }
    for (unsigned fail=1;fail<=assoc_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.fail_at=fail;assert(assoc_run()!=BLOCK_OK);erased();
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
        if (assoc.ie_changed) { assert(assoc.ie_restored || m.poisoned); }
        if (m.status_sets) { assert(state.confirmed && assoc.keys_erased); }
    }
    assoc_seed(ASSOC_NORMAL);m.mode=DOWN_FAIL;assert(assoc_run()!=BLOCK_OK && m.poisoned && !state.join.down && !assoc.ie_restored);erased();
    printf("PASS: verified-SAE-only 60-byte PMKID handoff, fixed SAE/CCMP/required-PMF RSN, padded IE snapshot/UP recheck/DOWN restoration, scoped event bits, bound association identity/security and BCDC EAPOL M1, early events/failed SET, wrong peers/interfaces/IDs, rejected association/deauth, %u operation failures, poisoning and private erasure; no EAPOL TX or key install\n",assoc_total);
    unsigned owner_total=0;
    for (enum owner_case mode=OWNER_NORMAL;mode<OWNER_COUNT;mode++) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.owner_case=mode;enum block_result r=assoc_run();
        if (mode<=OWNER_GET_UNSUPPORTED || mode==OWNER_INFO_EMPTY) {
            assert(r==BLOCK_OK && assoc.association_verified && assoc.assoc_ie_verified && assoc.ie_verified && assoc.ie_baseline_ack && assoc.ie_set_ack && assoc.ie_clear_ack);
            if (mode==OWNER_NORMAL) { owner_total=m.calls; }
            if (mode==OWNER_OPAQUE) { assert(assoc.ie_query_seen && !memcmp(assoc.ie_query_prefix,"wpaie",6)); }
            if (mode==OWNER_INFO_EMPTY) { assert(m.request_queries==3); }
        }else {
            assert(r!=BLOCK_OK);
            if (mode==OWNER_CLEAR_FAIL) { assert(m.poisoned && !assoc.ie_clear_ack && state.join.down); }
            else if (mode==OWNER_DEAUTH_IN_QUERY) { assert(!assoc.association_verified && assoc.assoc_ie_verified && assoc.failure_event==6); }
            else { assert(!assoc.association_verified && !assoc.assoc_ie_verified); }
            if (mode==OWNER_SET_FAIL) { assert(!m.ups && !m.status_sets); }
        }
        if (state.join.policy.attempted) { restored(); }
        else { assert(!m.up && !state.join.up_attempted && !m.status_sets); }
        erased();assert(!m.ie_bytes || mode==OWNER_CLEAR_FAIL);
    }
    for (unsigned fail=1;fail<=owner_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fail_at=fail;assert(assoc_run()!=BLOCK_OK);erased();
        if (assoc.ie_changed) { assert(assoc.ie_clear_ack || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    printf("PASS: exclusively owned fresh-station RSN SET/clear journal, opaque/unsupported diagnostic GET, actual association-request SSID/SAE/CCMP/PMF/PMKID validation, empty-info retry, rejected/missing/truncated/weaker request IEs, failed SET/clear and %u operation failures; no invented snapshot or readback claim\n",owner_total);
    peer.hunting=true;auth_seed(AUTH_NORMAL);assert(auth_run()==BLOCK_OK && state.auth_status==0 && state.confirmed);restored();erased();
    assoc_seed(ASSOC_NORMAL);m.ie_owner=true;assert(assoc_run()==BLOCK_OK && assoc.association_verified && assoc.m1_seen && state.auth_status==0);restored();erased();
    auth_seed(AUTH_BAD_HMAC);assert(auth_run()!=BLOCK_OK && !state.confirmed && state.crypto_result==SAE_CONFIRM);restored();erased();
    auth_seed(AUTH_NORMAL);m.wrong_method=true;assert(auth_run()==BLOCK_UNSUPPORTED && !state.confirmed && m.commit_txs==1 && !m.confirm_txs);restored();erased();
    peer.hunting=false;auth_seed(AUTH_NORMAL);m.wrong_method=true;assert(auth_run()==BLOCK_UNSUPPORTED && !state.confirmed && m.commit_txs==1 && !m.confirm_txs);restored();erased();
    puts("PASS: actual H&P-derived PWE through status-0 commit/confirm, firmware handoff and association/M1; bad confirm and cross-method status mismatches fail without method retry");
    peer.hunting=true;unsigned key_total=0;
    for (enum key_case mode=KEY_NORMAL;mode<KEY_COUNT;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fixed_request=true;m.key_probe=true;m.key_case=mode;enum block_result r=assoc_run();
        if (mode==KEY_NORMAL || mode==KEY_EARLY) {
            assert(r==BLOCK_OK && assoc.phase==7 && assoc.key_phase==4 && assoc.m2_written && assoc.m3_mic_verified && assoc.m3_unwrap_verified && assoc.unwrapped_bytes==16);
            if (mode==KEY_NORMAL) { key_total=m.calls; }
        }else {
            assert(r!=BLOCK_OK && !assoc.m3_unwrap_verified);
            if (mode==KEY_BAD_WRAP) { assert(assoc.m3_mic_verified && assoc.key_result==WPA_PROBE_UNWRAP); }
            else { assert(!assoc.m3_mic_verified); }
            if (mode==KEY_WRITE_FAIL) { assert(assoc.m3_bytes && !assoc.m2_written); }
            if (mode==KEY_RNG_FAIL) { assert(!m.m2_txs && !assoc.status_issued); }
        }
        assert(assoc.keys_erased && !assoc.private_exchange && !assoc.armed);restored();erased();
        unsigned calls=m.calls;(void)assoc_run();assert(m.calls==calls);
    }
    for (unsigned fail=1;fail<=key_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fixed_request=true;m.key_probe=true;m.fail_at=fail;
        assert(assoc_run()!=BLOCK_OK && !assoc.private_exchange && !assoc.armed);erased();
        if (m.status_sets) { assert(assoc.keys_erased); }
        if (assoc.ie_changed) { assert(assoc.ie_clear_ack || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    printf("PASS: full SAE/association -> M2 -> authenticated M3 MIC/unwrap model, early RX/failed write, RNG failure, wrong peer/replay/nonce/profile, corrupt MIC/wrap, bounded missing-M3 wait, %u operation failures, DOWN/clear/restore/private-key erasure; no M4/install\n",key_total);
    const enum owner_case bad_ies[]={OWNER_BAD_SSID,OWNER_WEAK_RSN,OWNER_DUP_RSN,OWNER_BAD_PMKID,OWNER_DEAUTH_IN_QUERY};
    for (unsigned i=0;i<sizeof(bad_ies)/sizeof(bad_ies[0]);++i) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fixed_request=true;m.key_probe=true;m.owner_case=bad_ies[i];
        assert(assoc_run()!=BLOCK_OK && !m.m2_txs && assoc.keys_erased && !assoc.private_exchange);restored();erased();
    }
    peer.hunting=false;assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fixed_request=true;m.key_probe=true;
    assert(assoc_run()==BLOCK_UNSUPPORTED && assoc.association_verified && !m.m2_txs && assoc.keys_erased);restored();erased();peer.hunting=true;
    unsigned kde_total=0;
    for (unsigned mode=0;mode<7;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=true;m.kde_mode=mode==6 ? 0 : mode;
        if (mode==6) { m.key_case=KEY_EARLY; }
        enum block_result r=assoc_run();
        if (!mode || mode==6) {
            assert(r==BLOCK_OK && assoc.phase==8 && assoc.key_phase==5 && assoc.key_data_verified && assoc.m4_encoded && assoc.m4_bytes==113 && m.m2_txs==1);
            assert(assoc.key_data.rsn_match && assoc.key_data.gtk_bytes==16 && assoc.key_data.igtk_bytes==16 && assoc.key_data.igtk_id==4);
            if (!mode) { kde_total=m.calls; }
        }else { assert(r!=BLOCK_OK && assoc.m3_mic_verified && assoc.m3_unwrap_verified && !assoc.key_data_verified && !assoc.m4_encoded && assoc.key_result==WPA_PROBE_KEY_DATA); }
        assert(assoc.keys_erased && !assoc.private_exchange);restored();erased();
    }
    for (unsigned fail=1;fail<=kde_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=true;m.fail_at=fail;
        assert(assoc_run()!=BLOCK_OK && !assoc.private_exchange && !assoc.armed);erased();
        if (m.status_sets) { assert(assoc.keys_erased); }if (assoc.ie_changed) { assert(assoc.ie_clear_ack || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    printf("PASS: authenticated AP-RSN/GTK/IGTK model, malformed/duplicate/missing KDE rejection after valid MIC/unwrap, offline M4 after validation only, early RX and %u operation failures with teardown/erasure; exactly one M2 TX and no M4 TX\n",kde_total);
    unsigned install_total=0,retry_total=0;
    for (unsigned mode=0;mode<23;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=true;m.install_case=mode;enum block_result r=assoc_run();
        if (mode==0 || mode==9 || (mode>=11 && mode<=15)) {
            if (r!=BLOCK_OK) { printf("Unexpected retry model failure: case=%u result=%u key=%u\n",mode,r,assoc.key_result); }
            assert(r==BLOCK_OK && assoc.phase==9 && assoc.key_phase==7 && assoc.keys.installed && assoc.keys.ack_mask==7 && assoc.m4_written && assoc.post_m4_identity && m.key_sets==3);
            assert(assoc.first_m4_before_keys && m.m4_txs==(mode==9 || mode==12 || mode==14 || mode==15 ? 2u : 1u));
            if (mode>=11) { assert(assoc.m3_retries_verified==1 && assoc.m3_retries_queued==1 && !assoc.m3_retry_pending); }
            if (mode==13) { assert(assoc.m4_deferred==1);retry_total=m.calls; }
            if (!mode) { install_total=m.calls; }
        }else { assert(r!=BLOCK_OK);if (mode<=3 || mode==5 || mode==10) { assert(assoc.first_m4_before_keys && m.m4_txs==1); }if (mode==6) { assert(m.poisoned && !assoc.keys.cleared && assoc.keys.clear_status==0xffffffe9u); } }
        if (mode!=6) { assert(assoc.keys.cleared && assoc.keys.clear_mask==assoc.keys.issued_mask); }
        assert(assoc.keys_erased && !assoc.private_exchange);restored();erased();
    }
    for (unsigned fail=1;fail<=retry_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=true;m.install_case=13;m.fail_at=fail;
        assert(assoc_run()!=BLOCK_OK && !assoc.private_exchange && !assoc.armed && m.key_sets<=3);erased();
        if (m.status_sets) { assert(assoc.keys_erased); }if (assoc.keys.issued_mask) { assert(assoc.keys.cleared || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    for (unsigned fail=1;fail<=install_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=true;m.fail_at=fail;
        assert(assoc_run()!=BLOCK_OK && !assoc.private_exchange && !assoc.armed);erased();
        if (m.status_sets) { assert(assoc.keys_erased); }if (assoc.keys.issued_mask) { assert(assoc.keys.cleared || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    printf("PASS: verified SAE/M3 -> initial live M4 -> three acknowledged firmware keys -> post-M4 identity -> DOWN/delete/restore; partial rejects, failed M4, deauth during install/after M4, changed identity, duplicate M3 without reinstall, failed deletion poisoning and %u operation failures; private keys erased\n",install_total);
    printf("PASS: captured third-M3 stop modeled during key SET/identity, higher-replay/MIC verification, credit deferral, after-write/poll M4 responses, unchanged keys/counters and no reinstall, bad MIC/changed GTK/RSC/stale replay/flood refusal and %u retry-path operation failures\n",retry_total);
    for (unsigned mode=0;mode<9;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=m.dhcp_probe=true;m.dhcp_case=mode;
        enum block_result r=assoc_run();
        if (!mode || mode==6) {
            assert(r==BLOCK_OK && assoc.phase==10 && assoc.key_phase==9 && assoc.dhcp.phase==DHCP_ACKED && assoc.dhcp.ack_seen && assoc.dhcp.request_written);
            assert(m.dhcp_txs==2 && m.key_sets==3 && assoc.dhcp.lease.address[3]==10);
            if (mode==6) { assert(m.m4_txs==2 && assoc.dhcp.deferrals==1 && assoc.m3_retries_verified==1); }
        }else { assert(r!=BLOCK_OK); }
        if (mode==1) { assert(assoc.dhcp.discover_sent==3 && !assoc.dhcp.have_offer); }
        if (mode==2) { assert(assoc.dhcp.request_sent==3 && !assoc.dhcp.ack_seen); }
        if (mode==3) { assert(assoc.dhcp.nak_seen); }
        if (mode==4) { assert(assoc.dhcp.ack_seen && !assoc.dhcp.request_written); }
        if (mode==5) { assert(!assoc.dhcp.attempted && !m.dhcp_txs); }
        if (mode==8) { assert(assoc.dhcp.phase==DHCP_ACKED); } /* final identity failure still fails overall */
        assert(!assoc.dhcp.armed && !assoc.private_exchange && assoc.keys.cleared && assoc.keys.clear_mask==7 && m.clear_sets==3 && m.key_sets==3);restored();erased();
    }
    puts("PASS: integrated verified SAE/M3/M4 -> DHCP DORA; lost OFFER/ACK, NAK, early ACK then failed write, RNG failure, disconnect, final identity failure and credit-time M3/M4 refresh without key reinstall; DOWN/delete/restore on every path");
    unsigned port_total=0;
    for (unsigned mode=0;mode<12;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=m.dhcp_probe=true;m.port_case=mode;enum block_result r=assoc_run();
        if (!mode || mode==11) {
            assert(r==BLOCK_OK && assoc.port_verified && assoc.port_closed && assoc.dhcp.phase==DHCP_ACKED && !m.authorized && assoc.first_m4_before_keys);
            assert(m.key_sets==3 && m.dhcp_txs==2 && m.port_closes==1);
            if (!mode) { port_total=m.calls; }else { assert(assoc.port_deferrals==1 && m.port_opens==2 && m.m4_txs==2); }
        }else { assert(r!=BLOCK_OK); }
        if (mode==5) { assert(assoc.port_verified && !assoc.port_closed && m.poisoned); }
        if (assoc.port_issued && mode!=5) { assert(assoc.port_closed && !m.authorized); }
        if (mode>=1 && mode<=4) { assert(!assoc.dhcp.attempted && !m.dhcp_txs); }
        assert(assoc.keys.cleared && !assoc.private_exchange && !assoc.dhcp.armed);restored();erased();
    }
    for (unsigned fail=1;fail<=port_total;++fail) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=m.dhcp_probe=true;m.fail_at=fail;
        assert(assoc_run()!=BLOCK_OK && !assoc.private_exchange && !assoc.armed);erased();
        if (assoc.port_issued) { assert(assoc.port_closed || m.poisoned); }
        if (assoc.keys.issued_mask) { assert(assoc.keys.cleared || m.poisoned); }
        if (state.join.policy.changed) { assert(state.join.policy.restored || m.poisoned); }
    }
    printf("PASS: initial M4 before TX keys; peer-bound port authorization/readback before DHCP, explicit deauthorization, rejected/stale/wrong-peer/unauthenticated SCB, lost authorization, credit-time M3 refresh and %u complete-path operation failures; cleanup/private erasure retained\n",port_total);
    unsigned gateway_points[8]={0};
    for (unsigned mode=0;mode<14;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=m.dhcp_probe=m.gateway_probe=true;m.gateway_case=mode;
        enum block_result r=assoc_run();
        if (!mode || mode==4) {
            assert(r==BLOCK_OK && assoc.phase==11 && assoc.network.phase==NP_DONE && assoc.network.received_mask==7 && assoc.network.echoes==3 && assoc.network.was_active && !assoc.network.active && m.key_sets==3);
            if (!mode) { memcpy(gateway_points,m.net_fail_points,sizeof(gateway_points)); }else { assert(assoc.network.deferrals==1 && assoc.m3_retries_verified==1 && m.m4_txs==2); }
        }else { assert(r!=BLOCK_OK); }
        if (mode==1 || mode==10 || mode==11) { assert(assoc.network.conflict && assoc.decline_attempted && m.declines==1 && assoc.decline_written==(mode!=10)); }
        else { assert(!assoc.decline_attempted); }
        if (mode==5 || mode==9 || mode==13) { assert(!m.net_calls); }
        if (mode==7) { assert(assoc.network.seen_mask && !assoc.network.written_mask && !assoc.network.received_mask); }
        assert(!assoc.network.active && !assoc.network.armed && !assoc.decline_active && assoc.port_closed && assoc.keys.cleared && m.key_sets==3 && !m.authorized && !assoc.private_exchange);restored();erased();
    }
    for (unsigned i=0;i<8;++i) { if (!gateway_points[i]) { continue; }
        assoc_seed(ASSOC_NORMAL);m.ie_owner=m.fixed_request=m.key_probe=m.key_data_probe=m.install_probe=m.dhcp_probe=m.gateway_probe=true;m.fail_at=gateway_points[i];
        assert(assoc_run()!=BLOCK_OK && !assoc.network.active && !assoc.network.armed && !assoc.private_exchange);erased();
        assert(assoc.port_closed || m.poisoned);assert(assoc.keys.cleared || m.poisoned);
    }
    puts("PASS: complete SAE/M4/port/DHCP -> address checks/ARP/three gateway echoes; conflicts and DHCPDECLINE, missing ARP/echo, no router, expired lease, RNG/write/deauth failures, early echo then failed write, bad checksum, credit-time M3 without reinstallation, packet-stage failure cleanup; temporary IP removed");
    struct brcm_auth_peer saved_peer=peer;memset(peer.ssid,0,sizeof(peer.ssid));memcpy(peer.ssid,"Jack's",6);peer.ssid_length=6;
    unsigned char dump[122],id[16]={0};captured(dump);struct brcm_assoc_probe diag={0};bool h2e;
    assert(brcm_assoc_request_check(&peer,id,dump,118,&h2e,&diag)==BLOCK_OK && diag.request_rsn_offset==32);
    assert(brcm_assoc_request_check(&peer,id,dump,122,&h2e,&diag)==BLOCK_UNSUPPORTED && diag.request_error==2 && diag.request_error_offset==118);
    for (unsigned mode=0;mode<3;++mode) {
        assoc_seed(ASSOC_NORMAL);m.ie_owner=true;m.fixed_request=true;m.captured_request=true;
        if (mode==1) { m.info_flags=1; }if (mode==2) { m.fixed_short=3; }
        enum block_result r=assoc_run();if (!mode) { assert(r==BLOCK_OK && assoc.assoc_ie_bytes==118 && assoc.assoc_info_words[0]==122 && assoc.association_verified); }
        else { assert(r!=BLOCK_OK && !assoc.association_verified); }restored();erased();
    }
    peer=saved_peer;puts("PASS: exact Pi 118-byte request + four-byte ABI header replay; strict parser still rejects trailing duplicate SSID, reassociation flags and length underflow");
    return 0;
}
