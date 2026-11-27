#include "rp1_eth_loop.h"
#include "rp1_probe.h"
#include "net_wire.h"
#include "host_assert.h"
#include "dhcp_fixture.h"
#include <stdio.h>
#include <string.h>

enum { NCR=0,NCFGR=4,TSR=0x14,RBQP=0x18,TBQP=0x1c,RSR=0x20,
       IMR=0x30,MID=0xfc,TX_FRAMES=0x108,RX_FRAMES=0x158,DCFG6=0x294,BUS=0x800000 };
struct fixture {
    struct rp1_eth_dma_memory memory;
    uint32_t regs[0x2a0/4];
    unsigned starts,cursor,last;
    bool pending,drop_setup,drop_stop;
};
static const uint8_t peer_mac[]={0x20,0x7b,0xd5,0x1a,0xc0,0x91},peer_ip[]={192,0,2,1};

static bool read(void *context,unsigned reg,uint32_t *value)
{
    struct fixture *f=context;
    *value=f->regs[reg/4];
    if (reg==TX_FRAMES || reg==RX_FRAMES) { f->regs[reg/4]=0; }
    return true;
}

static bool write(void *context,unsigned reg,uint32_t value)
{
    struct fixture *f=context;
    if (reg==NCR && !value && f->drop_stop) { return true; }
    if (reg==NCFGR && value==(0x20003|RP1_GEM_MDC_DIV_224) && f->drop_setup) { return true; }
    if (reg==TSR || reg==RSR) {
        f->regs[reg/4]&=~value;
    } else {
        f->regs[reg/4]=reg==NCR ? value&~0x200u : value;
    }
    if (reg==TBQP) { f->cursor=0;f->pending=false; }
    if (reg==NCR && (value&0x200)) {
        assert(value==(0x200|RP1_GEM_RX_TX|RP1_GEM_MPE));
        struct rp1_eth_dma_memory *m=&f->memory;
        assert(f->regs[RBQP/4]==BUS+offsetof(struct rp1_eth_dma_memory,rx));
        assert(f->regs[TBQP/4]==BUS+offsetof(struct rp1_eth_dma_memory,tx));
        assert(!f->pending && m->tx[f->cursor].address==BUS+offsetof(struct rp1_eth_dma_memory,frame));
        assert((m->tx[f->cursor].status&0xc0008000u)==(f->cursor ? 0x40008000u : 0x8000u));
        assert(m->tx[f->cursor^1].status&0x80000000u);
        f->last=f->cursor;
        f->pending=true;
        ++f->starts;
        f->regs[TSR/4]|=8;
    }
    return true;
}

static void barrier(void *context) { (void)context; }

static void complete(struct fixture *f)
{
    assert(f->pending);
    f->memory.tx[f->last].status|=0x80000000u;
    f->cursor^=1;
    f->pending=false;
    ++f->regs[TX_FRAMES/4];
    f->regs[TSR/4]=0x21;
}

static void receive(struct fixture *f,unsigned slot,size_t n)
{
    assert(!(f->memory.rx[slot].address&1));
    f->memory.rx[slot].status=(uint32_t)n|0xc000;
    f->memory.rx[slot].address|=1;
    f->regs[RSR/4]|=2;
    ++f->regs[RX_FRAMES/4];
}

static void reply_to_arp(struct fixture *f,unsigned slot)
{
    uint8_t *arp=f->memory.received[slot];
    memcpy(arp,rp1_eth_mac,6); memcpy(arp+6,peer_mac,6);
    memcpy(arp+12,f->memory.frame+12,8); net_put16(arp+20,2);
    memcpy(arp+22,peer_mac,6); memcpy(arp+28,peer_ip,4);
    memcpy(arp+32,rp1_eth_mac,6); memcpy(arp+38,rp1_eth_default_ipv4.address,4);
    receive(f,slot,60);
}

static void tcp_reply(struct fixture *f,const struct rp1_eth_dma *s,struct tcp_packet *reply)
{
    size_t n;
    assert(tcp_encode(reply,f->memory.received[s->head],GEM_RX_BYTES,&n));
    if (s->pending) { complete(f); }
    receive(f,s->head,n);
}

static void request_arp(struct fixture *f,unsigned slot,const struct ethernet_ipv4 *local)
{
    uint8_t *frame=f->memory.received[slot];
    static const uint8_t arp_header[]={8,6,0,1,8,0,6,4,0,1};
    memset(frame,0,60);
    memset(frame,255,6);
    memcpy(frame+6,peer_mac,6);
    memcpy(frame+12,arp_header,sizeof(arp_header));
    memcpy(frame+22,peer_mac,6);
    memcpy(frame+28,(const uint8_t[]){192,0,2,5},4);
    memcpy(frame+38,local->address,4);
    receive(f,slot,60);
}

static uint8_t *client_reply(struct fixture *f,unsigned slot,const uint8_t *fixture)
{
    uint8_t *reply=f->memory.received[slot];
    memcpy(reply,fixture,DHCP_FRAME_BYTES);
    memcpy(reply+42+28,rp1_eth_mac,6);
    for (size_t option=42+240;reply[option]!=255;) {
        unsigned type=reply[option++];
        if (!type) { continue; }
        unsigned len=reply[option++];
        if (type==61) { assert(len==7); memcpy(reply+option+1,rp1_eth_mac,6); }
        option+=len;
    }
    return reply;
}

struct dhcp_exchange {
    struct fixture *gem;
    struct rp1_eth_dma *dma;
    struct dhcp_client client;
    uint64_t now;
    bool reply,cancel,conflict;
};

static uint64_t dhcp_clock(void *context) { return ((struct dhcp_exchange *)context)->now; }
static bool dhcp_ready(void *context) { return !((struct dhcp_exchange *)context)->dma->ipv4.prefix; }
static enum block_result dhcp_poll(void *context)
{
    struct dhcp_exchange *x=context;
    return rp1_eth_dma_dhcp_poll(x->dma,&x->client,x->now);
}
static enum block_result dhcp_delay(void *context,unsigned us)
{
    struct dhcp_exchange *x=context;
    x->now+=us/1000;
    return x->cancel && x->client.discover_written ? BLOCK_NOT_READY : BLOCK_OK;
}

static enum block_result dhcp_transmit(void *context,const void *frame,size_t bytes)
{
    struct dhcp_exchange *x=context;
    const uint8_t *packet=frame;
    bool request=packet[42+242]==3;
    assert(net_be32(packet+26)==0 && net_be32(packet+30)==UINT32_MAX);
    assert(rp1_eth_dma_dhcp_send(x->dma,frame,bytes,x->now)==BLOCK_OK);
    assert(x->dma->pending && !memcmp(x->gem->memory.frame,frame,bytes));
    assert(rp1_eth_dma_dhcp_send(x->dma,frame,bytes,x->now)==BLOCK_NOT_READY);
    if (x->reply) {
        unsigned slot=x->dma->head;
        client_reply(x->gem,slot,request ? dhcp_ack_fixture : dhcp_offer_fixture);
        receive(x->gem,slot,DHCP_FRAME_BYTES);
    }
    complete(x->gem);
    return dhcp_poll(x);
}

static enum block_result claim_transmit(void *context,enum netprobe_packet kind,unsigned sequence,const void *frame,size_t bytes)
{
    struct dhcp_exchange *x=context;
    const uint8_t *packet=frame;
    struct ethernet_ipv4 local;
    assert(!sequence && (kind==NP_ARP_PROBE || kind==NP_ARP_ANNOUNCE));
    assert(bytes==60 && !memcmp(packet+38,x->client.lease.address,4));
    assert(net_be32(packet+28)==(kind==NP_ARP_PROBE ? 0 : net_be32(x->client.lease.address)));
    assert(rp1_eth_dma_claim_send(x->dma,kind,frame,bytes,x->now)==BLOCK_OK);
    if (x->conflict) {
        assert(ethernet_ipv4_configure(&local,x->client.lease.address,24));
        request_arp(x->gem,x->dma->head,&local);
        memset(x->gem->memory.received[x->dma->head]+28,0,4);
    }
    complete(x->gem);
    return dhcp_poll(x);
}

static void verify_claim(struct dhcp_exchange *x)
{
    struct netprobe claim={0};
    struct ethernet_ipv4 local;
    const uint8_t entropy[32]={[8]=1,[24]=1};
    uint8_t decline[DHCP_FRAME_BYTES];size_t bytes;
    struct rp1_eth_dma *dma=x->dma;
    struct fixture *gem=x->gem;
    dma->claim=&claim;
    assert(ethernet_ipv4_configure(&local,x->client.lease.address,24));
    assert(dhcp_encode_decline(&x->client,decline,sizeof(decline),&bytes)==BLOCK_OK);
    assert(rp1_eth_dma_dhcp_send(dma,decline,bytes,x->now)==BLOCK_ARGUMENT);
    const struct netprobe_io io={x,claim_transmit,dhcp_poll,dhcp_delay,dhcp_clock,dhcp_ready};
    x->client.lease.have_router=false;
    const uint64_t origin=x->now;
    enum block_result result=netprobe_claim(&io,rp1_eth_mac,&x->client.lease,entropy,origin,&claim);
    if (x->conflict) {
        assert(result==BLOCK_IO && claim.conflict && !claim.active && !claim.announcements);
        assert(!memcmp(claim.conflict_mac,peer_mac,6));
        assert(rp1_eth_dma_dhcp_send(dma,decline,bytes,x->now)==BLOCK_OK);
        assert(gem->memory.frame[42+242]==4);
        complete(gem);
        assert(dhcp_poll(x)==BLOCK_OK);
    } else if (x->client.lease.seconds==1) {
        assert(result==BLOCK_TIMEOUT && !claim.active && claim.probes==1 && !claim.announcements);
    } else {
        assert(result==BLOCK_OK && claim.active && claim.probes==3 && claim.announcements==2 && x->now-origin>=8000);
        dma->ipv4=local;
        dma->tcp_enabled=true;
        const uint8_t secret[32]={3};
        assert(udp_link_up(rp1_eth_mac,local.address,local.mask));
        assert(tcp_link_up(rp1_eth_mac,local.address,local.mask,secret));
        const struct uos_udp_peer peer={{192,0,2,1},4040,50000};
        const struct uos_tcp_peer tcp_peer={{192,0,2,1},4041,0};
        int64_t handle=udp_open(1,&peer);
        int64_t stream=tcp_open(1,&tcp_peer,x->now);
        assert(handle>0 && stream>0);
        request_arp(gem,dma->head,&local);
        assert(rp1_eth_dma_service(dma,x->now)==BLOCK_OK && dma->pending);
        assert(net_be16(gem->memory.frame+20)==2 && !memcmp(gem->memory.frame+28,local.address,4));
        complete(gem);
        request_arp(gem,dma->head,&local);
        memcpy(gem->memory.received[dma->head]+28,local.address,4);
        assert(rp1_eth_dma_service(dma,x->now)==BLOCK_IO && claim.conflict && !dma->pending);
        udp_link_down();
        tcp_link_down();
        assert(udp_ready(1,handle)==UOS_EVENT_HANGUP);
        assert(tcp_ready(1,stream)==UOS_EVENT_HANGUP);
    }
    netprobe_close(&claim);
    dma->claim=NULL;
}

static void test_dhcp(const struct rp1_dma_io *io,struct fixture *f,const uint32_t *original)
{
    const uint8_t entropy[10]={0x12,0x34,0x56,0x78};
    for (unsigned i=0;i<5;++i) {
        memcpy(f->regs,original,sizeof(f->regs));
        f->starts=0;
        struct rp1_eth_dma session;
        struct rp1_eth_dma_stats stats={0};
        struct dhcp_exchange x={.gem=f,.dma=&session,.reply=i!=1 && i!=2,.cancel=i==2,.conflict=i==3};
        assert(rp1_eth_dma_open(&session,io,&f->memory,BUS,100,NULL,false,&stats)==GEM_LOOP_OK);
        assert(rp1_eth_dma_service(&session,0)==BLOCK_NOT_READY);
        request_arp(f,session.head,&rp1_eth_default_ipv4);
        unsigned other=session.head^1;
        memset(f->memory.received[other],0,60);
        memset(f->memory.received[other],255,6);
        memcpy(f->memory.received[other]+6,peer_mac,6);
        net_put16(f->memory.received[other]+12,0x800);
        f->memory.received[other][14]=0x45;
        f->memory.received[other][23]=6;
        receive(f,other,60);
        assert(rp1_eth_dma_dhcp_send(&session,f->memory.received[session.head],60,0)==BLOCK_ARGUMENT);
        const struct dhcp_io probe={&x,dhcp_transmit,dhcp_poll,dhcp_delay,dhcp_clock,dhcp_ready};
        const enum block_result expected[]={BLOCK_OK,BLOCK_TIMEOUT,BLOCK_NOT_READY,BLOCK_OK,BLOCK_OK};
        const unsigned starts[]={2,3,1,2,2};
        assert(dhcp_probe_run(&probe,rp1_eth_mac,entropy,&x.client)==expected[i]);
        assert(!x.client.ignored && !x.client.malformed && x.client.received==(x.reply ? 2 : 0) && session.dhcp_other==2);
        assert(x.client.request_sent==(unsigned)x.reply && f->starts==starts[i]);
        if (x.reply) {
            assert(x.client.phase==DHCP_ACKED && x.client.lease.seconds==3600);
            assert(!memcmp(x.client.lease.address,(const uint8_t[]){192,0,2,10},4));
            if (i==4) { x.client.lease.seconds=1; }
            verify_claim(&x);
        }
        assert(rp1_eth_dma_close(&session) && !memcmp(original,f->regs,sizeof(f->regs)));
        const unsigned completed[]={8,3,1,4,3};
        assert(stats.tx_submitted==completed[i] && stats.tx_completed==completed[i] && stats.mac_tx==completed[i]);
    }
    puts("PASS: GEM DHCP filtering, offer/ACK, timeout, cancellation, ARP claim, conflict/DECLINE, expiry and DMA restoration.");
}

enum renewal_scenario { RENEW_T1,RENEW_T2,RENEW_MANUAL,RENEW_NAK };
struct renew_fixture {
    struct fixture *gem;
    struct rp1_eth_dma *dma;
    struct dhcp_renewal renewal;
    uint64_t now;
    unsigned seed;
    enum renewal_scenario scenario;
};

static uint64_t renew_clock(void *context) { return ((struct renew_fixture *)context)->now; }
static bool renew_entropy(void *context,uint8_t out[32])
{
    struct renew_fixture *r=context;
    memset(out,0,32);out[0]=(uint8_t)++r->seed;
    return true;
}

static enum block_result renew_transmit(void *context,const void *frame,size_t bytes)
{
    struct renew_fixture *r=context;
    struct rp1_eth_dma *dma=r->dma;
    struct dhcp_client *exchange=&r->renewal.exchange;
    const uint8_t *packet=frame;
    assert(net_be32(packet+26)==net_be32(dma->ipv4.address));
    assert(net_be32(packet+30)==(exchange->rebinding ? UINT32_MAX : net_be32(exchange->offer.server)));
    assert(rp1_eth_dma_dhcp_send(dma,frame,bytes,r->now)==BLOCK_ARGUMENT);
    enum block_result result=rp1_eth_dma_renew_send(dma,exchange,frame,bytes,r->now);
    if (result!=BLOCK_OK) { return result; }
    assert(dma->pending && !memcmp(r->gem->memory.frame,packet,bytes));
    if (r->scenario!=RENEW_T2 || exchange->rebinding) {
        unsigned slot=dma->head;
        uint8_t *reply=client_reply(r->gem,slot,dhcp_ack_fixture);
        memcpy(reply+42+4,packet+42+4,4);
        if (r->scenario==RENEW_NAK) {
            for (size_t at=42+240;reply[at]!=255;) {
                unsigned tag=reply[at++];
                if (tag==53) { reply[at+1]=6;break; }
                if (tag) { at+=1u+reply[at]; }
            }
        }
        receive(r->gem,slot,DHCP_FRAME_BYTES);
    }
    complete(r->gem);
    return rp1_eth_dma_lan_poll(dma,exchange,r->now);
}

static void test_renewals(const struct rp1_dma_io *io,struct fixture *f,const uint32_t *original)
{
    const uint8_t server_mac[]={2,0,0,0,1,1},local_ip[]={192,0,2,10};
    for (unsigned mode=RENEW_T1;mode<=RENEW_NAK;++mode) {
        struct rp1_eth_dma dma;
        struct rp1_eth_dma_stats stats={0};
        struct ethernet_ipv4 local;
        struct netprobe claim={.active=true};
        struct dhcp_lease lease={.seconds=60,.t1_seconds=20,.t2_seconds=45,.have_mask=true,.mask={255,255,255,0},
            .have_router=true,.router={192,0,2,1},.have_dns=true,.dns={8,8,8,8}};
        memcpy(lease.address,local_ip,4);memcpy(lease.server,peer_ip,4);memcpy(lease.server_mac,server_mac,6);
        uint64_t origin=100;
        struct renew_fixture r={.gem=f,.dma=&dma,.now=20099,.scenario=(enum renewal_scenario)mode};
        const struct dhcp_renew_io renew={&r,renew_transmit,renew_entropy,renew_clock};
        memcpy(f->regs,original,sizeof(f->regs));
        assert(ethernet_ipv4_configure(&local,local_ip,24));
        assert(rp1_eth_dma_open(&dma,io,&f->memory,BUS,100,&local,false,&stats)==GEM_LOOP_OK);
        dma.claim=&claim;
        assert(dhcp_renew_service(&renew,&r.renewal,rp1_eth_mac,&lease,&origin)==BLOCK_OK && !stats.tx_submitted);
        if (mode==RENEW_MANUAL) { r.now=1000;r.renewal.queued=true; }
        else { r.now=20100; }
        unsigned first=(unsigned)r.now;
        assert(dhcp_renew_service(&renew,&r.renewal,rp1_eth_mac,&lease,&origin)==BLOCK_OK);
        assert(stats.tx_completed==1 && r.renewal.requests==1 && origin==100);
        if (mode==RENEW_T2) {
            uint32_t xid=r.renewal.xid;
            request_arp(f,dma.head,&local);
            assert(rp1_eth_dma_lan_poll(&dma,&r.renewal.exchange,r.now+100)==BLOCK_OK && dma.pending);
            assert(net_be16(f->memory.frame+20)==2);complete(f);
            assert(rp1_eth_dma_lan_poll(&dma,&r.renewal.exchange,45099)==BLOCK_OK);
            r.now=45100;first=(unsigned)r.now;
            assert(dhcp_renew_service(&renew,&r.renewal,rp1_eth_mac,&lease,&origin)==BLOCK_OK);
            assert(r.renewal.xid!=xid && r.renewal.requests==2 && r.renewal.rebroadcasts==1);
        }
        ++r.now;
        enum block_result result=dhcp_renew_service(&renew,&r.renewal,rp1_eth_mac,&lease,&origin);
        if (mode==RENEW_NAK) {
            assert(result==BLOCK_IO && r.renewal.rejected && !r.renewal.completed && origin==100);
        } else {
            assert(result==BLOCK_OK && r.renewal.completed==1 && origin==first && lease.seconds==3600);
            assert(!memcmp(dma.ipv4.address,lease.address,4) && !r.renewal.exchange.armed);
        }
        assert(rp1_eth_dma_close(&dma) && !memcmp(original,f->regs,sizeof(f->regs)));
    }
    puts("PASS: GEM DHCP T1, T2, manual renewal, coexisting LAN traffic, NAK and DMA restoration.");
}

static size_t request_ping(struct fixture *f,unsigned slot,const struct ethernet_ipv4 *local,
                           unsigned payload,unsigned fragment)
{
    uint8_t *frame=f->memory.received[slot],*ip=frame+14,*icmp=ip+20;
    size_t length=42+payload;
    memset(frame,0,length);
    memcpy(frame,rp1_eth_mac,6);
    memcpy(frame+6,peer_mac,6);
    net_put16(frame+12,0x800);
    ip[0]=0x45;
    net_put16(ip+2,(unsigned)length-14);
    net_put16(ip+6,fragment);
    ip[8]=128;
    ip[9]=1;
    memcpy(ip+12,(const uint8_t[]){192,0,2,5},4);
    memcpy(ip+16,local->address,4);
    net_put16(ip+10,net_checksum(ip,20,0));
    icmp[0]=8;
    net_put16(icmp+4,0x39a0);
    net_put16(icmp+6,4);
    for (unsigned i=0;i<payload;++i) { icmp[8+i]=(uint8_t)i; }
    net_put16(icmp+2,net_checksum(icmp,8+payload,0));
    receive(f,slot,length);
    return length;
}

static void test_inbound(const struct rp1_dma_io *io,struct fixture *f,const uint32_t *original)
{
    memcpy(f->regs,original,sizeof(f->regs));
    f->starts=0;
    struct ethernet_ipv4 local;
    const uint8_t ip[]={192,0,2,6}, mask[]={255,255,255,252}, peer[]={192,0,2,5};
    assert(ethernet_ipv4_configure(&local,ip,30));
    assert(!ethernet_ipv4_configure(&local,ip,0));
    assert(!ethernet_ipv4_configure(&local,ip,31));
    assert(!ethernet_ipv4_configure(&local,(const uint8_t[]){192,0,2,4},30));
    assert(!ethernet_ipv4_configure(&local,(const uint8_t[]){192,0,2,7},30));
    assert(local.prefix==30 && !memcmp(local.address,ip,4) && !memcmp(local.mask,mask,4));
    struct rp1_eth_dma s;
    struct rp1_eth_dma_stats stats={0};
    f->regs[TX_FRAMES/4]=23;f->regs[RX_FRAMES/4]=35;
    assert(rp1_eth_dma_open(&s,io,&f->memory,BUS,100,&local,false,&stats)==GEM_LOOP_OK);
    assert(!(f->regs[NCFGR/4]&32) && udp_link_up(rp1_eth_mac,local.address,local.mask));
    uint8_t *reply=f->memory.frame;

    request_arp(f,s.head,&local);
    memcpy(f->memory.received[s.head]+38,rp1_eth_default_ipv4.address,4);
    assert(rp1_eth_dma_service(&s,0)==BLOCK_OK && !f->starts);
    request_arp(f,s.head,&local);
    f->regs[RSR/4]|=5;
    assert(rp1_eth_dma_service(&s,10)==BLOCK_OK && f->starts==1 && !f->regs[RSR/4]);
    assert((f->memory.tx[f->last].status&0x3fff)==60 && !memcmp(reply,peer_mac,6));
    assert(!memcmp(reply+6,rp1_eth_mac,6) && net_be16(reply+20)==2);
    assert(!memcmp(reply+22,rp1_eth_mac,6) && !memcmp(reply+28,ip,4));
    assert(!memcmp(reply+32,peer_mac,6) && !memcmp(reply+38,peer,4));
    for (unsigned i=42;i<60;++i) { assert(!reply[i]); }

    complete(f);
    size_t length=request_ping(f,s.head,&local,513,0x4000);
    assert(rp1_eth_dma_service(&s,20)==BLOCK_OK && f->starts==2 && s.pending);
    assert((f->memory.tx[f->last].status&0x3fff)==length && !memcmp(reply,peer_mac,6));
    assert(!memcmp(reply+6,rp1_eth_mac,6) && !memcmp(reply+26,ip,4) && !memcmp(reply+30,peer,4));
    assert(net_be16(reply+16)==length-14 && reply[22]==64 && !net_be16(reply+34));
    assert(!net_checksum(reply+14,20,0) && !net_checksum(reply+34,length-34,0));
    assert(net_be16(reply+38)==0x39a0 && net_be16(reply+40)==4);
    for (size_t i=42;i<length;++i) { assert(reply[i]==(uint8_t)(i-42)); }

    complete(f);
    request_ping(f,s.head,&local,3,0);
    assert(rp1_eth_dma_service(&s,30)==BLOCK_OK && f->starts==2 && !s.pending);
    request_ping(f,s.head,&local,3,0x2000);
    assert(rp1_eth_dma_service(&s,120)==BLOCK_OK && f->starts==2);
    request_ping(f,s.head,&local,3,0);
    f->memory.received[s.head][42]^=1;
    assert(rp1_eth_dma_service(&s,130)==BLOCK_OK && f->starts==2);
    request_ping(f,s.head,&local,525,0);
    assert(rp1_eth_dma_service(&s,140)==BLOCK_OK && f->starts==2);
    receive(f,s.head,1600);
    assert(rp1_eth_dma_service(&s,145)==BLOCK_OK && stats.rx_discarded==1);
    request_ping(f,s.head,&local,3,0);
    assert(rp1_eth_dma_service(&s,150)==BLOCK_OK && f->starts==3 && s.pending);
    assert((f->memory.tx[f->last].status&0x3fff)==60);
    complete(f);
    const struct uos_udp_peer old_peer={{192,0,2,1},4040,50000},new_peer={{192,0,2,5},4040,50000};
    assert(udp_open(1,&old_peer)<0);
    int64_t handle=udp_open(1,&new_peer);
    assert(handle>0 && udp_send(1,handle,"test",4,160)==4);
    assert(rp1_eth_dma_service(&s,160)==BLOCK_OK && f->starts==4);
    assert(net_be16(reply+20)==1 && !memcmp(reply+28,ip,4) && !memcmp(reply+38,peer,4));
    complete(f);
    f->regs[RSR/4]=8;
    assert(rp1_eth_dma_service(&s,170)==BLOCK_IO);
    rp1_eth_dma_sample(&s);
    rp1_eth_dma_sample(&s);
    assert(rp1_eth_dma_close(&s) && !memcmp(original,f->regs,sizeof(f->regs)));
    assert(stats.tx_submitted==4 && stats.tx_completed==4 && stats.mac_tx==4 && !stats.tx_timeouts);
    assert(stats.rx_frames==8 && stats.mac_rx==9 && stats.rx_overruns==1 && !stats.mac_unavailable);
    udp_link_down();
    puts("PASS: configured Ethernet IPv4, ARP and ICMP replies, RX overrun recovery, input checks and rate limits.");
}

static void test_tcp(const struct rp1_dma_io *io,struct fixture *f,const uint32_t *original)
{
    memset(&f->memory,0,sizeof(f->memory));
    memcpy(f->regs,original,sizeof(f->regs));
    f->starts=0;
    struct rp1_eth_dma s;
    struct rp1_eth_dma_stats stats={0};
    assert(rp1_eth_dma_open(&s,io,&f->memory,BUS,100,&rp1_eth_default_ipv4,true,&stats)==GEM_LOOP_OK);
    const uint8_t secret[32]={3};
    assert(tcp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask,secret));
    assert(udp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask));
    const struct uos_tcp_peer tcp_peer={{192,0,2,1},4041,0};
    const struct uos_udp_peer udp_peer={{192,0,2,1},4040,50000};
    int64_t tcp=tcp_open(1,&tcp_peer,0),udp=udp_open(1,&udp_peer);
    assert(tcp>0 && udp>0 && udp_send(1,udp,"udp",3,0)==3);
    uint8_t *frame=f->memory.frame;
    assert(rp1_eth_dma_service(&s,0)==BLOCK_OK && f->starts==1 && s.tcp_first);
    assert(net_be16(frame+12)==0x806);
    complete(f);
    assert(rp1_eth_dma_service(&s,10)==BLOCK_OK && f->starts==2 && !s.tcp_first);
    assert(net_be16(frame+12)==0x806);
    reply_to_arp(f,s.head);
    complete(f);
    assert(rp1_eth_dma_service(&s,20)==BLOCK_OK && f->starts==3 && s.tcp_first);
    assert(frame[23]==17 && net_be16(frame+36)==4040);
    const struct udp_packet echo={peer_mac,peer_ip,rp1_eth_mac,rp1_eth_default_ipv4.address,4040,50000,"udp",3};
    size_t n;
    assert(udp_encode(&echo,f->memory.received[s.head],GEM_RX_BYTES,&n));
    receive(f,s.head,n);
    complete(f);
    assert(rp1_eth_dma_service(&s,30)==BLOCK_OK && f->starts==4 && !s.tcp_first);
    char datagram[3];
    assert(udp_recv(1,udp,datagram,sizeof(datagram))==3 && !memcmp(datagram,"udp",3));
    assert(frame[23]==6 && frame[47]==TCP_SYN && net_be16(frame+36)==4041);
    struct tcp_packet reply={.local_mac=peer_mac,.local_ip=peer_ip,.peer_mac=rp1_eth_mac,.peer_ip=rp1_eth_default_ipv4.address,
        .local_port=4041,.peer_port=(uint16_t)net_be16(frame+34),.window=1024,
        .seq=1000,.ack=net_be32(frame+38)+1,.flags=TCP_SYN|TCP_ACK};
    tcp_reply(f,&s,&reply);
    assert(rp1_eth_dma_service(&s,40)==BLOCK_OK && frame[47]==TCP_ACK && tcp_status(1,tcp)==0);
    ++reply.seq;
    for (unsigned offset=0;offset<16384;offset+=512) {
        uint64_t ms=50+offset/512*20;
        uint8_t data[512],returned[512];
        for (unsigned i=0;i<sizeof(data);++i) { data[i]=(uint8_t)((offset+i)^(offset>>4)); }
        assert(tcp_write(1,tcp,data,sizeof(data))==sizeof(data));
        complete(f);
        assert(rp1_eth_dma_service(&s,ms)==BLOCK_OK && s.pending && frame[47]==(TCP_PSH|TCP_ACK));
        assert(net_be32(frame+38)==reply.ack && !memcmp(frame+54,data,sizeof(data)));
        reply.ack+=sizeof(data);
        reply.flags=TCP_PSH|TCP_ACK;
        reply.payload=data;
        reply.bytes=sizeof(data);
        tcp_reply(f,&s,&reply);
        assert(rp1_eth_dma_service(&s,ms+10)==BLOCK_OK && s.pending && frame[47]==TCP_ACK);
        assert(tcp_read(1,tcp,returned,sizeof(returned))==sizeof(returned) && !memcmp(data,returned,sizeof(data)));
        reply.seq+=sizeof(data);
    }
    reply.bytes=0; reply.payload=NULL; reply.flags=TCP_FIN|TCP_ACK;
    tcp_reply(f,&s,&reply);
    assert(rp1_eth_dma_service(&s,700)==BLOCK_OK && frame[47]==TCP_ACK);
    uint8_t byte;
    assert(tcp_read(1,tcp,&byte,1)==0 && tcp_close(1,tcp,710)==0);
    complete(f);
    assert(rp1_eth_dma_service(&s,710)==BLOCK_OK && frame[47]==(TCP_FIN|TCP_ACK));
    ++reply.seq; ++reply.ack; reply.flags=TCP_ACK;
    tcp_reply(f,&s,&reply);
    assert(rp1_eth_dma_service(&s,720)==BLOCK_OK && !s.pending);
    tcp=tcp_open(1,&tcp_peer,730);
    assert(tcp>0 && tcp_status(1,tcp)==UOS_EAGAIN);
    struct uos_tcp_stats tcp_stats_before,tcp_stats_after;
    struct uos_udp_stats udp_stats_before,udp_stats_after;
    tcp_stats(&tcp_stats_before);udp_stats(&udp_stats_before);
    tcp_link_down(); udp_link_down();
    assert(tcp_status(1,tcp)==UOS_ENETDOWN && udp_recv(1,udp,datagram,sizeof(datagram))==UOS_ENETDOWN);
    assert(tcp_ready(1,tcp)==UOS_EVENT_HANGUP && udp_ready(1,udp)==UOS_EVENT_HANGUP);
    assert(rp1_eth_dma_close(&s) && !memcmp(original,f->regs,sizeof(f->regs)));
    puts("PASS: shared GEM UDP/TCP transmit order, 16 KiB stream, FIN/EOF, closed handles and DMA restoration.");
    unsigned submissions=f->starts;
    assert(rp1_eth_dma_open(&s,io,&f->memory,BUS,1000,&rp1_eth_default_ipv4,true,&stats)==GEM_LOOP_OK);
    assert(!s.tx_head && stats.tx_submitted==submissions && stats.tx_completed==submissions && stats.mac_tx==submissions);
    assert((f->regs[NCFGR/4]&0x401)==0x400);
    assert(tcp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask,secret));
    assert(udp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask));
    assert(tcp_status(1,tcp)==UOS_EBADF && udp_recv(1,udp,datagram,sizeof(datagram))==UOS_EBADF);
    tcp_stats(&tcp_stats_after);udp_stats(&udp_stats_after);
    assert(tcp_stats_after.session.generation==tcp_stats_before.session.generation+1 && !tcp_stats_after.session.used);
    assert(udp_stats_after.session.generation==udp_stats_before.session.generation+1 && !udp_stats_after.session.used);
    assert(!tcp_stats_after.bytes_rx && !udp_stats_after.bytes_rx);
    tcp_link_down();udp_link_down();
    assert(rp1_eth_dma_close(&s) && !memcmp(original,f->regs,sizeof(f->regs)));
    puts("PASS: GEM reopened at a different speed with new transport generations and invalid old handles.");
}

int main(void)
{
    struct fixture f={0};
    f.regs[IMR/4]=0x3fffffff;
    f.regs[MID/4]=0x70109;
    f.regs[DCFG6/4]=0x08820000;
    uint32_t original[sizeof(f.regs)/sizeof(f.regs[0])];
    memcpy(original,f.regs,sizeof(original));
    const struct rp1_dma_io io={&f,read,write,NULL,barrier};
    struct rp1_eth_dma s;
    struct rp1_eth_dma_stats stats={0};
    f.drop_setup=true;
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,&rp1_eth_default_ipv4,false,&stats)==GEM_LOOP_SETUP);
    assert(!memcmp(original,f.regs,sizeof(original)));
    f.drop_setup=false;
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,&rp1_eth_default_ipv4,false,&stats)==GEM_LOOP_OK);
    assert(f.regs[NCR/4]==(RP1_GEM_RX_TX|RP1_GEM_MPE) && !(f.memory.rx[0].address&3) && (f.memory.rx[1].address&3)==2);
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,&rp1_eth_default_ipv4,false,&stats)==GEM_LOOP_BUSY);
    assert(udp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask));
    const struct uos_udp_peer peer={{192,0,2,1},4040,50000};
    int64_t handle=udp_open(1,&peer);
    assert(handle>0);
    const uint8_t message[]={0x42,0,0x51};
    assert(udp_send(1,handle,message,sizeof(message),0)==sizeof(message));
    assert(rp1_eth_dma_service(&s,0)==BLOCK_OK && f.starts==1 && s.pending);
    assert(net_be16(f.memory.frame+12)==0x806 && net_be16(f.memory.frame+20)==1);
    assert(rp1_eth_dma_service(&s,10)==BLOCK_OK && f.starts==1);
    reply_to_arp(&f,0);
    complete(&f);
    assert(rp1_eth_dma_service(&s,20)==BLOCK_OK && f.starts==2 && s.head==1 && s.pending);
    assert(!f.memory.rx[0].status && !(f.memory.rx[0].address&1));
    assert(net_be16(f.memory.frame+12)==0x800 && net_be16(f.memory.frame+36)==4040);
    const struct udp_packet echo={peer_mac,peer_ip,rp1_eth_mac,rp1_eth_default_ipv4.address,4040,50000,message,sizeof(message)};
    uint8_t actual[sizeof(message)];
    size_t n;
    for (unsigned i=0;i<3;++i) {
        unsigned slot=(i+1)&1;
        uint64_t now=30+i*20;
        assert(udp_encode(&echo,f.memory.received[slot],GEM_RX_BYTES,&n));
        complete(&f);
        receive(&f,slot,n);
        assert(rp1_eth_dma_service(&s,now)==BLOCK_OK && !s.pending && s.head==(slot^1));
        assert((f.memory.rx[slot].address&3)==slot*2 && !f.memory.rx[slot].status);
        assert(udp_recv(1,handle,actual,sizeof(actual))==sizeof(actual) && !memcmp(actual,message,sizeof(actual)));
        if (i<2) {
            assert(udp_send(1,handle,message,sizeof(message),now)==sizeof(message));
            assert(rp1_eth_dma_service(&s,now+10)==BLOCK_OK && f.starts==3+i && s.pending);
        }
    }
    f.drop_stop=true;
    assert(!rp1_eth_dma_close(&s) && f.regs[RBQP/4]!=original[RBQP/4]);
    f.drop_stop=false;
    assert(rp1_eth_dma_close(&s) && !memcmp(original,f.regs,sizeof(original)));
    udp_link_down();
    assert(udp_recv(1,handle,actual,sizeof(actual))==UOS_ENETDOWN);

    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,&rp1_eth_default_ipv4,false,&stats)==GEM_LOOP_OK);
    f.memory.tx[1].address^=4;
    assert(rp1_eth_dma_service(&s,0)==BLOCK_IO && !rp1_eth_dma_close(&s));
    f.memory.tx[1].address^=4;
    assert(rp1_eth_dma_close(&s));
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,&rp1_eth_default_ipv4,false,&stats)==GEM_LOOP_OK);
    assert(udp_link_up(rp1_eth_mac,rp1_eth_default_ipv4.address,rp1_eth_default_ipv4.mask));
    handle=udp_open(1,&peer);
    assert(handle>0 && udp_send(1,handle,message,sizeof(message),0)==sizeof(message));
    assert(rp1_eth_dma_service(&s,0)==BLOCK_OK && s.pending);
    assert(rp1_eth_dma_service(&s,100)==BLOCK_TIMEOUT && stats.tx_timeouts==1);
    assert(!rp1_eth_dma_close(&s) && f.regs[RBQP/4]!=original[RBQP/4]);
    udp_link_down();
    puts("PASS: persistent GEM UDP ARP/echo, TX and RX wrap, socket cleanup, timeout and DMA retention.");
    test_tcp(&io,&f,original);
    test_inbound(&io,&f,original);
    puts("PASS: GEM TX ring rotation, idle-slot guards, DMA and MAC counters, and retained stats across reopen.");
    test_dhcp(&io,&f,original);
    test_renewals(&io,&f,original);
}
