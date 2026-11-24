#include "rp1_eth_loop.h"
#include "net_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { NCR=0,NCFGR=4,TSR=0x14,RBQP=0x18,TBQP=0x1c,RSR=0x20,
       IMR=0x30,MID=0xfc,DCFG6=0x294,BUS=0x800000 };
struct fixture {
    struct rp1_eth_dma_memory memory;
    uint32_t regs[0x2a0/4];
    unsigned starts;
    bool drop_setup,drop_stop;
};
static const uint8_t peer_mac[]={0x20,0x7b,0xd5,0x1a,0xc0,0x91},peer_ip[]={192,0,2,1};

static bool read(void *context,unsigned reg,uint32_t *value)
{
    struct fixture *f=context;
    *value=f->regs[reg/4];
    return true;
}

static bool write(void *context,unsigned reg,uint32_t value)
{
    struct fixture *f=context;
    if (reg==NCR && !value && f->drop_stop) { return true; }
    if (reg==NCFGR && value==0x20023 && f->drop_setup) { return true; }
    if (reg==TSR || reg==RSR) {
        f->regs[reg/4]&=~value;
    } else {
        f->regs[reg/4]=reg==NCR ? value&~0x200u : value;
    }
    if (reg==NCR && value==0x20c) {
        struct rp1_eth_dma_memory *m=&f->memory;
        assert(f->regs[RBQP/4]==BUS+offsetof(struct rp1_eth_dma_memory,rx));
        assert(f->regs[TBQP/4]==BUS+offsetof(struct rp1_eth_dma_memory,tx));
        assert(m->tx.address==BUS+offsetof(struct rp1_eth_dma_memory,frame));
        assert((m->tx.status&0xc0008000u)==0x40008000u);
        ++f->starts;
        f->regs[TSR/4]|=8;
    }
    return true;
}

static void barrier(void *context) { (void)context; }

static void complete(struct fixture *f)
{
    f->memory.tx.status|=0x80000000u;
    f->regs[TSR/4]=0x21;
}

static void receive(struct fixture *f,unsigned slot,size_t n)
{
    assert(!(f->memory.rx[slot].address&1));
    f->memory.rx[slot].status=(uint32_t)n|0xc000;
    f->memory.rx[slot].address|=1;
    f->regs[RSR/4]|=2;
}

static void reply_to_arp(struct fixture *f,unsigned slot)
{
    uint8_t *arp=f->memory.received[slot];
    memcpy(arp,rp1_eth_mac,6); memcpy(arp+6,peer_mac,6);
    memcpy(arp+12,f->memory.frame+12,8); net_put16(arp+20,2);
    memcpy(arp+22,peer_mac,6); memcpy(arp+28,peer_ip,4);
    memcpy(arp+32,rp1_eth_mac,6); memcpy(arp+38,rp1_eth_ip,4);
    receive(f,slot,60);
}

static void tcp_reply(struct fixture *f,const struct rp1_eth_dma *s,struct tcp_packet *reply)
{
    size_t n;
    assert(tcp_encode(reply,f->memory.received[s->head],GEM_RX_BYTES,&n));
    if (s->pending) { complete(f); }
    receive(f,s->head,n);
}

static void test_tcp(const struct rp1_dma_io *io,struct fixture *f,const uint32_t *original)
{
    memset(&f->memory,0,sizeof(f->memory));
    memcpy(f->regs,original,sizeof(f->regs));
    f->starts=0;
    struct rp1_eth_dma s;
    assert(rp1_eth_dma_open(&s,io,&f->memory,BUS,100,true)==GEM_LOOP_OK);
    const uint8_t secret[32]={3};
    assert(tcp_link_up(rp1_eth_mac,rp1_eth_ip,rp1_eth_mask,secret));
    assert(udp_link_up(rp1_eth_mac,rp1_eth_ip,rp1_eth_mask));
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
    const struct udp_packet echo={peer_mac,peer_ip,rp1_eth_mac,rp1_eth_ip,4040,50000,"udp",3};
    size_t n;
    assert(udp_encode(&echo,f->memory.received[s.head],GEM_RX_BYTES,&n));
    receive(f,s.head,n);
    complete(f);
    assert(rp1_eth_dma_service(&s,30)==BLOCK_OK && f->starts==4 && !s.tcp_first);
    char datagram[3];
    assert(udp_recv(1,udp,datagram,sizeof(datagram))==3 && !memcmp(datagram,"udp",3));
    assert(frame[23]==6 && frame[47]==TCP_SYN && net_be16(frame+36)==4041);
    struct tcp_packet reply={.local_mac=peer_mac,.local_ip=peer_ip,.peer_mac=rp1_eth_mac,.peer_ip=rp1_eth_ip,
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
    tcp_link_down(); udp_link_down();
    assert(tcp_status(1,tcp)==UOS_ENETDOWN && udp_recv(1,udp,datagram,sizeof(datagram))==UOS_ENETDOWN);
    assert(rp1_eth_dma_close(&s) && !memcmp(original,f->regs,sizeof(f->regs)));
    puts("PASS: shared GEM UDP/TCP transmit order, 16 KiB stream, FIN/EOF, closed handles and DMA restoration.");
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
    f.drop_setup=true;
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,false)==GEM_LOOP_SETUP);
    assert(!memcmp(original,f.regs,sizeof(original)));
    f.drop_setup=false;
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,false)==GEM_LOOP_OK);
    assert(f.regs[NCR/4]==0xc && !(f.memory.rx[0].address&3) && (f.memory.rx[1].address&3)==2);
    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,false)==GEM_LOOP_BUSY);
    assert(udp_link_up(rp1_eth_mac,rp1_eth_ip,rp1_eth_mask));
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
    const struct udp_packet echo={peer_mac,peer_ip,rp1_eth_mac,rp1_eth_ip,4040,50000,message,sizeof(message)};
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

    assert(rp1_eth_dma_open(&s,&io,&f.memory,BUS,100,false)==GEM_LOOP_OK);
    assert(udp_link_up(rp1_eth_mac,rp1_eth_ip,rp1_eth_mask));
    handle=udp_open(1,&peer);
    assert(handle>0 && udp_send(1,handle,message,sizeof(message),0)==sizeof(message));
    assert(rp1_eth_dma_service(&s,0)==BLOCK_OK && s.pending);
    assert(rp1_eth_dma_service(&s,100)==BLOCK_TIMEOUT);
    assert(!rp1_eth_dma_close(&s) && f.regs[RBQP/4]!=original[RBQP/4]);
    udp_link_down();
    puts("PASS: persistent GEM UDP ARP/echo, TX and RX wrap, socket cleanup, timeout and DMA retention.");
    test_tcp(&io,&f,original);
}
