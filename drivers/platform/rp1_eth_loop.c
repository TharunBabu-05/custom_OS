#include "rp1_eth_loop.h"
#include "net_wire.h"
#include <stddef.h>

/* Cadence GEM_GXL queue zero registers and legacy descriptor bits. */
enum {
    NCR=0, NCFGR=4, DMACFG=0x10, TSR=0x14, RBQP=0x18, TBQP=0x1c, RSR=0x20,
    IMR=0x30, SA1_LOW=0x88, SA1_HIGH=0x8c, MID=0xfc, DCFG6=0x294,
    LOOPBACK=2, RX_ENABLE=4, TX_ENABLE=8, TX_START=1<<9,
    TX_ACTIVE=1<<3, TX_DONE=1<<5, TX_W1C=0x1f7, RX_W1C=0xf,
    DMA_IRQS=0xcf6,
    TX_WRAP=1<<30, TX_LAST=1<<15, RX_USED=1, RX_WRAP=2, RX_WHOLE=3<<14
};
#define TX_USED (1u<<31)
#define TX_ERRORS (TX_W1C&~(TX_DONE|1u))
#define RX_ERRORS 0xc

const uint8_t rp1_eth_mac[]={2,0,0,0,0x18,2};
const uint8_t rp1_eth_ip[]={192,0,2,2},rp1_eth_mask[]={255,255,255,252};

/* Locally administered 02:00:00:00:18:02, Pi 192.0.2.2, PC 192.0.2.1. */
static const uint8_t arp_request[]={
    255,255,255,255,255,255, 2,0,0,0,0x18,2, 8,6,
    0,1, 8,0, 6,4, 0,1,
    2,0,0,0,0x18,2, 192,0,2,2, 0,0,0,0,0,0, 192,0,2,1
};

static bool set(const struct rp1_dma_io *io,struct rp1_eth_loop_report *r,unsigned reg,uint32_t value)
{
    uint32_t observed=UINT32_MAX;
    bool written=io->write(io->context,reg,value);
    bool readable=io->read(io->context,reg,&observed);
    if (written && readable && observed==value) { return true; }
    if (r && !r->register_failed) {
        r->register_failed=true;
        r->offset=reg;
        r->expected=value;
        r->observed=readable ? observed : UINT32_MAX;
    }
    return false;
}

static bool save_gem(const struct rp1_dma_io *io,struct gem_registers *s,struct rp1_eth_loop_report *r,bool wire)
{
    uint32_t id,ncr,queues,mask;
    void *ctx=io->context;
    if (!io->read(ctx,MID,&id) || id!=0x70109 || !io->read(ctx,DCFG6,&queues)) { return false; }
    if (r) { r->queues=queues; }
    if ((queues&0xfe) || !io->read(ctx,IMR,&mask)) { return false; }
    if (r) { r->mask=mask; }
    return (mask&DMA_IRQS)==DMA_IRQS && io->read(ctx,NCR,&ncr) && !ncr &&
           io->read(ctx,TSR,&s->tx_status) && !(s->tx_status&TX_ACTIVE) &&
           io->read(ctx,RSR,&s->rx_status) &&
           io->read(ctx,NCFGR,&s->cfg) && io->read(ctx,DMACFG,&s->dma) &&
           io->read(ctx,TBQP,&s->tx_base) && io->read(ctx,RBQP,&s->rx_base) &&
           (!wire || (io->read(ctx,SA1_LOW,&s->sa_low) && io->read(ctx,SA1_HIGH,&s->sa_high)));
}

static bool program_gem(const struct rp1_dma_io *io,const struct gem_registers *s,struct rp1_eth_loop_report *r,
                        unsigned speed,uint32_t tx,uint32_t rx)
{
    /* Preserve the firmware's RGMII routing, bus width, MDC and SRAM size.
     * Cable mode filters unicast replies. Loopback accepts its own frame. */
    uint32_t cfg=(s->cfg&(31u<<18)) | (1u<<17) | (1u<<1);
    cfg|=speed ? 1u<<5 : 1u<<4;
    if (!speed || speed==1000) { cfg|=1u<<10; }
    if (speed==100) { cfg|=1; }
    uint32_t dma=(s->dma&~((7u<<28)|(255u<<16)|(1u<<11)|(7u<<5))) | (GEM_RX_BYTES/64u<<16);
    return (!speed || (set(io,r,SA1_LOW,2) && set(io,r,SA1_HIGH,0x0218))) &&
           set(io,r,NCFGR,cfg) && set(io,r,DMACFG,dma) && set(io,r,TBQP,tx) && set(io,r,RBQP,rx);
}

static bool restore_gem(const struct rp1_dma_io *io,const struct gem_registers *s,
                        struct rp1_eth_loop_report *r,bool wire)
{
    return set(io,r,TBQP,s->tx_base) && set(io,r,RBQP,s->rx_base) &&
           set(io,r,DMACFG,s->dma) && set(io,r,NCFGR,s->cfg) &&
           (!wire || (set(io,r,SA1_LOW,s->sa_low) && set(io,r,SA1_HIGH,s->sa_high)));
}

static bool ack_status(const struct rp1_dma_io *io,const struct gem_registers *s,uint32_t tx,uint32_t rx)
{
    uint32_t clear_tx=tx&~s->tx_status&TX_W1C;
    uint32_t clear_rx=rx&~s->rx_status&RX_W1C;
    uint32_t now_tx,now_rx;
    void *ctx=io->context;
    return io->write(ctx,TSR,clear_tx) && io->write(ctx,RSR,clear_rx) &&
           io->read(ctx,TSR,&now_tx) && io->read(ctx,RSR,&now_rx) &&
           !(now_tx&(clear_tx|TX_ACTIVE)) && !(now_rx&clear_rx);
}

static uint8_t frame_byte(unsigned i,unsigned seed)
{
    if (i<6) { return 0xff; }
    if (i==6) { return 2; }
    if (i==12) { return 0x88; }
    if (i==13) { return 0xb5; }
    return (uint8_t)(seed+i*29);
}

static bool check_frame(const volatile struct rp1_eth_loop_memory *m,unsigned seed)
{
    for (unsigned i=0;i<GEM_FRAME_BYTES;++i) {
        if (m->received[i]!=frame_byte(i,seed) || m->frame[i]!=m->received[i]) { return false; }
    }
    return true;
}

static uint64_t arp_peer(const volatile uint8_t *p,unsigned bytes)
{
    if (bytes<sizeof(arp_request) || bytes>GEM_RX_BYTES || (p[6]&1) || p[21]!=2) { return 0; }
    for (unsigned i=12;i<21;++i) {
        if (p[i]!=arp_request[i]) { return 0; }
    }
    uint64_t peer=0;
    for (unsigned i=0;i<6;++i) {
        if (p[i]!=arp_request[6+i] || p[32+i]!=p[i] || p[22+i]!=p[6+i]) { return 0; }
        peer=peer<<8|p[6+i];
    }
    for (unsigned i=0;i<4;++i) {
        if (p[28+i]!=arp_request[38+i] || p[38+i]!=arp_request[28+i]) { return 0; }
    }
    return peer==0x020000001802ULL ? 0 : peer;
}

enum { ETH_HEADER=14, IPV4_HEADER=20, ECHO_BYTES=26, IP_BYTES=IPV4_HEADER+ECHO_BYTES,
       ECHO_FRAME=ETH_HEADER+IP_BYTES };

static void build_echo(volatile uint8_t *out,uint64_t peer,unsigned seed)
{
    uint8_t frame[ECHO_FRAME]={0};
    uint8_t *ip=frame+ETH_HEADER,*icmp=ip+IPV4_HEADER;
    for (unsigned i=0;i<6;++i) {
        frame[i]=(uint8_t)(peer>>(8*(5-i)));
        frame[6+i]=arp_request[6+i];
    }
    frame[12]=8;
    ip[0]=0x45;
    ip[3]=IP_BYTES;
    ip[6]=0x40;
    ip[8]=64;
    ip[9]=1;
    for (unsigned i=0;i<4;++i) {
        ip[12+i]=arp_request[28+i];
        ip[16+i]=arp_request[38+i];
    }
    icmp[0]=8;
    net_put32(icmp+4,seed);
    for (unsigned i=8;i<ECHO_BYTES;++i) {
        icmp[i]=(uint8_t)((seed>>((i&3)*8))^i);
    }
    net_put16(icmp+2,net_checksum(icmp,ECHO_BYTES,0));
    net_put16(ip+10,net_checksum(ip,IPV4_HEADER,0));
    for (unsigned i=0;i<sizeof(frame);++i) { out[i]=frame[i]; }
}

static bool echo_reply(const volatile uint8_t *received,const volatile uint8_t *request,unsigned bytes)
{
    if (bytes<ECHO_FRAME || bytes>GEM_RX_BYTES) { return false; }
    uint8_t frame[ECHO_FRAME];
    for (unsigned i=0;i<sizeof(frame);++i) { frame[i]=received[i]; }
    const uint8_t *ip=frame+ETH_HEADER,*icmp=ip+IPV4_HEADER;
    if (net_be16(frame+12)!=0x800 || ip[0]!=0x45 || net_be16(ip+2)!=IP_BYTES ||
        (net_be16(ip+6)&0xbfff) || !ip[8] || ip[9]!=1 || net_be16(icmp) ||
        net_checksum(ip,IPV4_HEADER,0) || net_checksum(icmp,ECHO_BYTES,0)) { return false; }
    for (unsigned i=0;i<6;++i) {
        if (frame[i]!=request[6+i] || frame[6+i]!=request[i]) { return false; }
    }
    for (unsigned i=0;i<4;++i) {
        if (ip[12+i]!=arp_request[38+i] || ip[16+i]!=arp_request[28+i]) { return false; }
    }
    for (unsigned i=ETH_HEADER+IPV4_HEADER+4;i<ECHO_FRAME;++i) {
        if (frame[i]!=request[i]) { return false; }
    }
    return true;
}

static enum rp1_eth_loop_result exchange(const struct rp1_dma_io *io,volatile struct rp1_eth_loop_memory *m,
                                         uint64_t bus,uint64_t timeout,unsigned speed,uint64_t peer,
                                         struct rp1_eth_loop_report *r)
{
    *r=(struct rp1_eth_loop_report){0};
    struct gem_registers saved;
    void *ctx=io->context;
    if ((speed && speed!=10 && speed!=100 && speed!=1000) ||
        (bus&63) || bus>UINT32_MAX-(sizeof(*m)-1) || !timeout ||
        !save_gem(io,&saved,r,speed!=0)) {
        return GEM_LOOP_BUSY;
    }
    uint32_t frame_bus=(uint32_t)(bus+offsetof(struct rp1_eth_loop_memory,frame));
    uint32_t received_bus=(uint32_t)(bus+offsetof(struct rp1_eth_loop_memory,received));
    m->tx[0]=(struct gem_descriptor){frame_bus,TX_LAST|(peer ? ECHO_FRAME : GEM_FRAME_BYTES)};
    m->tx[1]=(struct gem_descriptor){frame_bus,TX_USED|TX_WRAP};
    m->rx[0]=(struct gem_descriptor){received_bus,0};
    m->rx[1]=(struct gem_descriptor){received_bus|RX_USED|RX_WRAP,0};
    unsigned seed=(unsigned)io->ticks(ctx);
    if (peer) {
        build_echo(m->frame,peer,seed);
    } else {
        for (unsigned i=0;i<GEM_FRAME_BYTES;++i) {
            m->frame[i]=speed ? (i<sizeof(arp_request) ? arp_request[i] : 0) : frame_byte(i,seed);
        }
    }
    for (unsigned i=0;i<GEM_RX_BYTES;++i) { m->received[i]=0; }
    for (unsigned i=0;i<GEM_GUARD_BYTES;++i) { m->before[i]=0xa5; m->after[i]=0x5a; }
    io->barrier(ctx);

    enum rp1_eth_loop_result result=GEM_LOOP_SETUP;
    bool armed=false,completed=false;
    unsigned mode=speed ? 0 : LOOPBACK;
    if (program_gem(io,&saved,r,speed,(uint32_t)(bus+offsetof(struct rp1_eth_loop_memory,tx)),
                    (uint32_t)(bus+offsetof(struct rp1_eth_loop_memory,rx))) &&
        set(io,r,NCR,mode)) {
        armed=true;
        const unsigned active=mode|RX_ENABLE|TX_ENABLE;
        if (set(io,r,NCR,active) && io->write(ctx,NCR,active|TX_START)) {
            uint64_t start=io->ticks(ctx);
            do {
                io->barrier(ctx);
                r->tx=m->tx[0].status;
                r->rx=m->rx[0].status;
                if (!io->read(ctx,TSR,&r->tsr) || !io->read(ctx,RSR,&r->rsr)) { break; }
                completed=(r->tx&TX_USED) && m->rx[0].address==(received_bus|RX_USED) &&
                          (r->tsr&(TX_DONE|TX_ACTIVE))==TX_DONE;
            } while (!completed && io->ticks(ctx)-start<timeout);
        }
    }

    /* A timeout can leave a bus write in flight. Disable the MAC and keep
     * the scratch page reserved unless both DMA descriptors were returned. */
    if (!set(io,r,NCR,0) || (armed && !completed)) { return GEM_LOOP_RETAIN; }
    io->barrier(ctx);
    if (armed) {
        /* Read RX metadata again after acquiring ownership and stopping GEM. */
        r->rx=m->rx[0].status;
        if (!io->read(ctx,TSR,&r->tsr) || !io->read(ctx,RSR,&r->rsr)) { return GEM_LOOP_RETAIN; }
        result=GEM_LOOP_FRAME;
        if ((r->rx&RX_WHOLE)==RX_WHOLE &&
            !(r->tx&(7u<<27)) && !(r->tsr&TX_ERRORS) && !(r->rsr&RX_ERRORS)) {
            unsigned bytes=r->rx&0xfff;
            r->peer_mac=peer ? peer : speed ? arp_peer(m->received,bytes) : 0;
            if (peer ? echo_reply(m->received,m->frame,bytes) :
                r->peer_mac || (!speed && bytes==GEM_FRAME_BYTES && check_frame(m,seed))) {
                result=GEM_LOOP_OK;
            }
            for (unsigned i=0;i<GEM_GUARD_BYTES;++i) {
                if (m->before[i]!=0xa5 || m->after[i]!=0x5a) { result=GEM_LOOP_FRAME; break; }
            }
        }
        if (!ack_status(io,&saved,r->tsr,r->rsr)) { return GEM_LOOP_RETAIN; }
    }
    return restore_gem(io,&saved,r,speed!=0) ? result : GEM_LOOP_RETAIN;
}

enum rp1_eth_loop_result rp1_eth_exchange(const struct rp1_dma_io *io,volatile struct rp1_eth_loop_memory *m,
                                        uint64_t bus,uint64_t timeout,unsigned speed,struct rp1_eth_loop_report *r)
{
    return exchange(io,m,bus,timeout,speed,0,r);
}

enum rp1_eth_loop_result rp1_eth_ping(const struct rp1_dma_io *io,volatile struct rp1_eth_loop_memory *m,
                                    uint64_t bus,uint64_t timeout,unsigned speed,struct rp1_eth_loop_report *r)
{
    if (!speed) {
        *r=(struct rp1_eth_loop_report){0};
        return GEM_LOOP_BUSY;
    }
    enum rp1_eth_loop_result result=exchange(io,m,bus,timeout,speed,0,r);
    if (result==GEM_LOOP_OK && r->peer_mac) {
        result=exchange(io,m,bus,timeout,speed,r->peer_mac,r);
        r->echo=true;
    }
    return result;
}

static uint32_t rx_address(const struct rp1_eth_dma *s,unsigned slot)
{
    return (s->bus+offsetof(struct rp1_eth_dma_memory,received)+slot*GEM_RX_BYTES)|(slot ? RX_WRAP : 0);
}

enum rp1_eth_loop_result rp1_eth_dma_open(struct rp1_eth_dma *s,const struct rp1_dma_io *io,
                                         struct rp1_eth_dma_memory *m,uint32_t bus,unsigned speed,bool tcp_enabled)
{
    if ((speed!=10 && speed!=100 && speed!=1000) || (bus&63) ||
        bus>UINT32_MAX-(sizeof(*m)-1) || !save_gem(io,&s->saved,NULL,true)) {
        return GEM_LOOP_BUSY;
    }
    s->io=io;
    s->memory=m;
    s->bus=bus;
    s->head=0;
    s->pending=false;
    s->tcp_enabled=tcp_enabled;
    s->tcp_first=false;
    m->tx=(struct gem_descriptor){bus+offsetof(struct rp1_eth_dma_memory,frame),TX_USED|TX_WRAP};
    for (unsigned i=0;i<2;++i) {
        m->rx[i]=(struct gem_descriptor){rx_address(s,i),0};
    }
    io->barrier(io->context);
    if (program_gem(io,&s->saved,NULL,speed,bus+offsetof(struct rp1_eth_dma_memory,tx),
                    bus+offsetof(struct rp1_eth_dma_memory,rx)) &&
        set(io,NULL,NCR,RX_ENABLE|TX_ENABLE)) { return GEM_LOOP_OK; }
    return rp1_eth_dma_close(s) ? GEM_LOOP_SETUP : GEM_LOOP_RETAIN;
}

static enum block_result send_frame(struct rp1_eth_dma *s,size_t bytes,bool tcp)
{
    const struct rp1_dma_io *io=s->io;
    io->barrier(io->context);
    s->memory->tx.status=TX_LAST|TX_WRAP|(uint32_t)bytes;
    io->barrier(io->context);
    s->pending=true;
    s->tcp_first=!tcp;
    return io->write(io->context,NCR,RX_ENABLE|TX_ENABLE|TX_START) ? BLOCK_OK : BLOCK_IO;
}

static enum block_result send_udp(void *context,const struct udp_packet *packet)
{
    struct rp1_eth_dma *s=context;
    if (s->pending) { return BLOCK_NOT_READY; }
    size_t bytes;
    return udp_encode(packet,s->memory->frame,sizeof(s->memory->frame),&bytes) ?
           send_frame(s,bytes,false) : BLOCK_IO;
}

static enum block_result send_tcp(void *context,const struct tcp_packet *packet)
{
    struct rp1_eth_dma *s=context;
    if (s->pending) { return BLOCK_NOT_READY; }
    size_t bytes;
    return tcp_encode(packet,s->memory->frame,sizeof(s->memory->frame),&bytes) ?
           send_frame(s,bytes,true) : BLOCK_IO;
}

enum block_result rp1_eth_dma_service(struct rp1_eth_dma *s,uint64_t now_ms)
{
    const struct rp1_dma_io *io=s->io;
    void *ctx=io->context;
    uint32_t tx,rx;
    if (!io->read(ctx,TSR,&tx) || !io->read(ctx,RSR,&rx) || tx==UINT32_MAX || rx==UINT32_MAX ||
        (tx&~s->saved.tx_status&TX_ERRORS) || (rx&~s->saved.rx_status&RX_ERRORS)) { return BLOCK_IO; }
    io->barrier(ctx);
    if (s->pending) {
        uint32_t status=s->memory->tx.status;
        if ((status&TX_USED) && (tx&(TX_ACTIVE|TX_DONE))==TX_DONE) {
            if ((status&(7u<<27)) || !io->write(ctx,TSR,tx&~s->saved.tx_status&TX_W1C)) { return BLOCK_IO; }
            s->pending=false;
        } else if (now_ms-s->submitted>=100) {
            return BLOCK_TIMEOUT;
        }
    }
    for (unsigned i=0;i<2;++i) {
        unsigned slot=s->head;
        volatile struct gem_descriptor *descriptor=&s->memory->rx[slot];
        uint32_t address=descriptor->address;
        if (!(address&RX_USED)) { break; }
        uint32_t expected=rx_address(s,slot);
        if (address!=(expected|RX_USED)) { return BLOCK_IO; }
        io->barrier(ctx);
        uint32_t status=descriptor->status;
        if ((status&RX_WHOLE)==RX_WHOLE) {
            const void *frame=s->memory->received[slot];
            unsigned bytes=status&0xfff;
            udp_receive(frame,bytes,now_ms);
            if (s->tcp_enabled) { tcp_receive(frame,bytes); }
        }
        descriptor->status=0;
        io->barrier(ctx);
        descriptor->address=expected;
        s->head^=1;
    }
    if (rx&~s->saved.rx_status&RX_W1C) {
        if (!io->write(ctx,RSR,rx&~s->saved.rx_status&RX_W1C)) { return BLOCK_IO; }
    }
    if (!s->pending) { s->submitted=now_ms; }
    /* Give the other protocol first use of the shared TX slot next time. */
    bool tcp_first=s->tcp_enabled && s->tcp_first;
    enum block_result result=tcp_first ? tcp_service(send_tcp,s,now_ms) : BLOCK_OK;
    if (result==BLOCK_OK) { result=udp_service(send_udp,s,now_ms); }
    return result==BLOCK_OK && s->tcp_enabled && !tcp_first ? tcp_service(send_tcp,s,now_ms) : result;
}

bool rp1_eth_dma_close(struct rp1_eth_dma *s)
{
    const struct rp1_dma_io *io=s->io;
    void *ctx=io->context;
    uint32_t tx,rx;
    /* Disable the engines before sampling status and returning DMA memory.
     * An outstanding transmit must have returned descriptor ownership. */
    if (!set(io,NULL,NCR,0) || !io->read(ctx,TSR,&tx) || !io->read(ctx,RSR,&rx) ||
        tx==UINT32_MAX || rx==UINT32_MAX || (tx&TX_ACTIVE)) { return false; }
    io->barrier(ctx);
    return (s->memory->tx.status&TX_USED) && ack_status(io,&s->saved,tx,rx) &&
           restore_gem(io,&s->saved,NULL,true);
}
