#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "brcm_control.h"
#include "dhcp.h"
static struct brcm_control_state s;
static struct { unsigned calls,fail_at,writes,reads,delays;bool inject,fail_write,cancel,cancel_on_rx;unsigned char sent[388];size_t bytes; } m;
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static enum block_result rd(void *ctx,uint32_t at,uint32_t *v)
{ assert(ctx==&m);enum block_result r=touch();*v=at==0x18004020 && m.inject ? 0x40 : 0;return r; }
static enum block_result wr(void *ctx,uint32_t at,uint32_t v)
{ assert(ctx==&m && at==0x18004020 && v==0x40);return touch(); }
static enum block_result packet(void *ctx,bool write,void *data,size_t n)
{
    assert(ctx==&m);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }
    if (write) { ++m.writes;assert(n<=388 && !(n%4));memcpy(m.sent,data,n);m.bytes=n;return m.fail_write ? BLOCK_IO : BLOCK_OK; }
    assert(m.inject && n==64);++m.reads;m.inject=false;if (m.cancel_on_rx) { m.cancel=true; }memset(data,0,n);unsigned char *p=data;p[0]=12;p[2]=0xf3;p[3]=0xff;p[7]=12;p[9]=8;return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000);++m.delays;return touch(); }
static const struct brcm_control_io io={.context=&m,.read32=rd,.write32=wr,.packet=packet,.delay_us=delay};
static bool permitted(void *ctx) { assert(ctx==&m);return !m.cancel; }
static enum block_result send(const void *frame,size_t n) { return brcm_control_eapol_m2(&io,&s,frame,n,permitted,&m); }
static void seed(void)
{ memset(&s,0,sizeof(s));memset(&m,0,sizeof(m));s.ready=s.f2_ready=s.device_ready=true;s.tx_seq=7;s.tx_max=8;s.request_id=51; }
static void fixture(unsigned char frame[141])
{ memset(frame,0,141);frame[0]=frame[6]=2;frame[5]=2;frame[11]=1;frame[12]=0x88;frame[13]=0x8e;frame[14]=2;frame[15]=3;frame[17]=123;frame[18]=2;frame[19]=1;frame[20]=8;frame[112]=28; }
int main(void)
{
    unsigned char frame[141];fixture(frame);seed();assert(send(frame,141)==BLOCK_OK && m.writes==1 && !m.reads && !m.delays);
    assert(s.request_id==51 && s.tx_seq==8 && s.tx_frames==1 && !s.waiting && s.ready && m.bytes==160);
    struct brcm_sdpcm_header h;assert(brcm_sdpcm_header(m.sent,m.bytes,&h)==BLOCK_OK && h.length==157 && h.channel==2 && h.sequence==7 && h.offset==12);
    assert(m.sent[12]==0x20 && !m.sent[13] && !m.sent[14] && !m.sent[15] && !memcmp(m.sent+16,frame,141));for (unsigned i=157;i<160;++i) { assert(!m.sent[i]); }
    unsigned total=m.calls;for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(send(frame,141)==BLOCK_IO && !s.ready && s.tx_seq==7 && !s.tx_frames); }
    seed();m.fail_write=true;assert(send(frame,141)==BLOCK_IO && !s.ready && s.request_id==51);
    seed();s.tx_max=7;assert(send(frame,141)==BLOCK_TIMEOUT && !m.writes && m.delays==500 && !s.ready);
    seed();s.tx_max=7;m.inject=true;memcpy(s.frame,frame,141);assert(send(s.frame,141)==BLOCK_OK && m.reads==1 && m.writes==1 && !memcmp(m.sent+16,frame,141));
    for (unsigned i=0;i<6;++i) {
        seed();fixture(frame);if (!i) { frame[20]=0; }if (i==1) { frame[12]=8; }if (i==2) { frame[0]|=1; }if (i==3) { frame[17]=122; }if (i==4) { frame[112]=27; }if (i==5) { memcpy(frame+6,frame,6); }
        assert(send(frame,141)==BLOCK_ARGUMENT && !m.calls && s.ready);
    }
    seed();fixture(frame);m.inject=true;m.cancel=true;assert(send(frame,141)==BLOCK_NOT_READY && !m.writes && s.ready);
    seed();fixture(frame);frame[17]=95;frame[19]=3;frame[112]=0;assert(send(frame,113)==BLOCK_ARGUMENT && !m.calls && !m.writes); /* Properly sized M4 refused. */
    assert(brcm_control_eapol_m4(&io,&s,frame,113,permitted,&m)==BLOCK_OK && m.writes==1 && m.bytes==132 && m.sent[5]==2 && m.sent[35]==3);
    seed();m.cancel=true;assert(brcm_control_eapol_m4(&io,&s,frame,113,permitted,&m)==BLOCK_NOT_READY && !m.writes && s.ready);
    seed();m.inject=m.cancel_on_rx=true;assert(brcm_control_eapol_m4(&io,&s,frame,113,permitted,&m)==BLOCK_NOT_READY && !m.writes && m.reads==1 && s.ready);
    seed();frame[31]=1;assert(brcm_control_eapol_m4(&io,&s,frame,113,permitted,&m)==BLOCK_ARGUMENT && !m.calls);
    seed();fixture(frame);s.waiting=true;assert(send(frame,141)==BLOCK_NOT_READY && !m.calls);
    unsigned char dhcp[DHCP_FRAME_BYTES];const unsigned char mac[6]={2,0,0,0,0,1},entropy[10]={1,2,3,4};struct dhcp_client client={0};size_t n=0;
    assert(dhcp_client_init(&client,mac,entropy)==BLOCK_OK && dhcp_encode(&client,false,0,dhcp,sizeof(dhcp),&n)==BLOCK_OK);
    seed();assert(brcm_control_dhcp(&io,&s,dhcp,n,permitted,&m)==BLOCK_OK && m.writes==1 && m.bytes==360 && s.request_id==51 && s.tx_seq==8 && !s.waiting);
    assert(brcm_sdpcm_header(m.sent,m.bytes,&h)==BLOCK_OK && h.length==358 && h.channel==2 && h.offset==12 && !memcmp(m.sent+16,dhcp,n) && !m.sent[358] && !m.sent[359]);
    total=m.calls;for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(brcm_control_dhcp(&io,&s,dhcp,n,permitted,&m)==BLOCK_IO && !s.ready && s.tx_seq==7); }
    seed();m.inject=m.cancel_on_rx=true;assert(brcm_control_dhcp(&io,&s,dhcp,n,permitted,&m)==BLOCK_NOT_READY && !m.writes && m.reads==1 && s.ready);
    seed();dhcp[40]^=1;assert(brcm_control_dhcp(&io,&s,dhcp,n,permitted,&m)==BLOCK_ARGUMENT && !m.calls);
    struct netprobe np={0};unsigned char random[32],net[74];for (unsigned i=0;i<32;++i) { random[i]=(unsigned char)(i+1); }
    const struct dhcp_lease lease={.address={192,0,2,10},.router={192,0,2,1},.server={192,0,2,1},.mask={255,255,255,0},.seconds=3600,.have_mask=true,.have_router=true};
    assert(netprobe_init(&np,mac,&lease,random,0)==BLOCK_OK && netprobe_encode(&np,NP_ARP_PROBE,0,net,sizeof(net),&n)==BLOCK_OK);
    seed();assert(brcm_control_netprobe(&io,&s,&np,NP_ARP_PROBE,0,net,n,permitted,&m)==BLOCK_OK && m.writes==1 && m.bytes==76 && s.request_id==51 && !s.waiting);
    assert(brcm_sdpcm_header(m.sent,m.bytes,&h)==BLOCK_OK && h.length==76 && h.channel==2 && !memcmp(m.sent+16,net,n));
    total=m.calls;for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(brcm_control_netprobe(&io,&s,&np,NP_ARP_PROBE,0,net,n,permitted,&m)==BLOCK_IO && !s.ready); }
    np.active=np.gateway_known=true;np.phase=NP_PING;np.gateway_mac[0]=2;np.gateway_mac[5]=2;
    assert(netprobe_encode(&np,NP_ECHO,1,net,sizeof(net),&n)==BLOCK_OK);seed();assert(brcm_control_netprobe(&io,&s,&np,NP_ECHO,1,net,n,permitted,&m)==BLOCK_OK && m.bytes==92 && !m.sent[90] && !m.sent[91] && !memcmp(m.sent+16,net,n));
    seed();m.inject=m.cancel_on_rx=true;assert(brcm_control_netprobe(&io,&s,&np,NP_ECHO,1,net,n,permitted,&m)==BLOCK_NOT_READY && !m.writes && m.reads==1 && s.ready);
    seed();net[36]^=1;assert(brcm_control_netprobe(&io,&s,&np,NP_ECHO,1,net,n,permitted,&m)==BLOCK_ARGUMENT && !m.calls);
    client.phase=DHCP_ACKED;client.ack_seen=client.request_written=true;client.lease=lease;
    assert(dhcp_encode_decline(&client,dhcp,sizeof(dhcp),&n)==BLOCK_OK);seed();assert(brcm_control_dhcp(&io,&s,dhcp,n,permitted,&m)==BLOCK_OK && m.sent[300]==4 && m.bytes==360 && s.request_id==51);
    puts("PASS: guarded SAE M2/M4, typed DHCP/DECLINE and owned ARP/ICMP BCDC/SDPCM frames, padding, credit wait, shared-RX staging, cancellation after RX, no CDC request/reply, failure poisoning and invalid-frame refusal; no generic data interface");return 0;
}
