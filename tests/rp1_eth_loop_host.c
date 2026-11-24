#include "rp1_eth_loop.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

enum { NCR=0,CFG=4,USRIO=0xc,DMA=0x10,TSR=0x14,RBQP=0x18,TBQP=0x1c,RSR=0x20,
       IMR=0x30,SA1_LOW=0x88,SA1_HIGH=0x8c,MID=0xfc,DCFG6=0x294,BUS=0x800000 };
enum fault { NONE,NO_LOOP_BIT,SHORT_FRAME,BAD_FRAME,BAD_GUARD,NO_RX,NO_RESTORE,DROP_FILTER,
             SHORT_ECHO,BAD_IP_SUM,BAD_ECHO_SUM,WRONG_ECHO_ID,WRONG_ECHO_PAYLOAD,WRONG_IP,WRONG_MAC,
             FRAGMENT,NO_ECHO };
struct fixture {
    struct rp1_eth_loop_memory memory;
    uint32_t regs[0x2a0/4];
    unsigned writes,started,clock,speed,corrupt;
    enum fault fault;
};

static bool read(void *context,unsigned reg,uint32_t *value)
{
    struct fixture *f=context;
    *value=f->regs[reg/4];
    return true;
}

static uint16_t checksum(const uint8_t *bytes,unsigned length)
{
    unsigned sum=0;
    for (unsigned i=0;i<length;++i) { sum+=(unsigned)bytes[i]<<((i&1) ? 0 : 8); }
    while (sum>0xffff) { sum=(sum&0xffff)+(sum>>16); }
    return (uint16_t)~sum;
}

static void fix_checksum(uint8_t *bytes,unsigned length,unsigned offset)
{
    bytes[offset]=bytes[offset+1]=0;
    uint16_t sum=checksum(bytes,length);
    bytes[offset]=(uint8_t)(sum>>8);
    bytes[offset+1]=(uint8_t)sum;
}

static void make_echo_reply(struct fixture *f)
{
    struct rp1_eth_loop_memory *m=&f->memory;
    static const uint8_t header[]={2,3,4,5,6,7, 2,0,0,0,24,2, 8,0,
        0x45,0,0,46,0,0,0x40,0,64,1,0,0, 192,0,2,2, 192,0,2,1, 8,0};
    assert(!memcmp(m->frame,header,24) && !memcmp(m->frame+26,header+26,sizeof(header)-26));
    assert(!checksum(m->frame+14,20) && !checksum(m->frame+34,26));
    memcpy(m->received,m->frame,60);
    memcpy(m->received,m->frame+6,6);
    memcpy(m->received+6,m->frame,6);
    memcpy(m->received+26,m->frame+30,4);
    memcpy(m->received+30,m->frame+26,4);
    m->received[22]=128;
    m->received[34]=0;
    if (f->fault==WRONG_MAC) { m->received[7]^=1; }
    if (f->fault==WRONG_IP) { m->received[29]^=1; }
    if (f->fault==FRAGMENT) { m->received[20]=0x20; }
    if (f->fault==WRONG_ECHO_ID) { m->received[38]^=1; }
    if (f->fault==WRONG_ECHO_PAYLOAD) { m->received[59]^=1; }
    fix_checksum(m->received+14,20,10);
    fix_checksum(m->received+34,26,2);
    if (f->fault==BAD_IP_SUM) { m->received[24]^=1; }
    if (f->fault==BAD_ECHO_SUM) { m->received[36]^=1; }
}

static bool write(void *context,unsigned reg,uint32_t value)
{
    struct fixture *f=context;
    struct rp1_eth_loop_memory *m=&f->memory;
    ++f->writes;
    if (reg==USRIO) { return true; } /* Fixed interface, RGMII selector does not latch. */
    if (reg==SA1_HIGH && value==0x218 && f->fault==DROP_FILTER) { return true; }
    if (f->started && f->fault==NO_RESTORE && reg==TBQP) { return false; }
    if (reg==TSR || reg==RSR) {
        f->regs[reg/4]&=~value;
    } else if (reg!=NCR) {
        f->regs[reg/4]=value;
    } else {
        f->regs[NCR/4]=value&~(1u<<9);
        if (f->fault==NO_LOOP_BIT) { f->regs[NCR/4]&=~2u; }
        if (!(value&(1u<<9))) { return true; }
        unsigned active=f->speed ? 0xc : 0xe;
        assert(value==(0x200|active) && f->regs[NCR/4]==active);
        ++f->started;
        assert(f->regs[TBQP/4]==BUS+offsetof(struct rp1_eth_loop_memory,tx));
        assert(f->regs[RBQP/4]==BUS+offsetof(struct rp1_eth_loop_memory,rx));
        assert(f->regs[DMA/4]==0x00180704);
        assert(m->tx[0].address==BUS+offsetof(struct rp1_eth_loop_memory,frame));
        assert(m->rx[0].address==BUS+offsetof(struct rp1_eth_loop_memory,received));
        bool echo=f->speed && m->frame[13]==0;
        assert(m->tx[0].status==(0x8000|(echo ? 60 : GEM_FRAME_BYTES)) && m->tx[1].status==0xc0000000);
        assert((m->rx[1].address&3)==3);
        unsigned bytes=GEM_FRAME_BYTES;
        if (echo) {
            assert(f->started==2);
            make_echo_reply(f);
            bytes=f->fault==SHORT_ECHO ? 59 : 60;
        } else if (f->speed) {
            static const uint8_t request[]={
                255,255,255,255,255,255, 2,0,0,0,24,2, 8,6,
                0,1,8,0,6,4,0,1, 2,0,0,0,24,2, 192,0,2,2,
                0,0,0,0,0,0, 192,0,2,1
            };
            static const uint8_t reply[]={
                2,0,0,0,24,2, 2,3,4,5,6,7, 8,6,
                0,1,8,0,6,4,0,2, 2,3,4,5,6,7, 192,0,2,1,
                2,0,0,0,24,2, 192,0,2,2
            };
            assert(!memcmp(m->frame,request,sizeof(request)));
            for (unsigned i=sizeof(request);i<GEM_FRAME_BYTES;++i) { assert(!m->frame[i]); }
            assert(f->regs[SA1_LOW/4]==2 && f->regs[SA1_HIGH/4]==0x218);
            assert(f->regs[CFG/4]==(0x2a0022|(f->speed==1000 ? 1u<<10 : f->speed==100)));
            memcpy(m->received,reply,sizeof(reply));
            if (f->corrupt) { m->received[f->corrupt-1]^=1; }
            bytes=60;
        } else {
            assert(m->frame[0]==0xff && m->frame[6]==2 && m->frame[12]==0x88 && m->frame[13]==0xb5);
            memcpy(m->received,m->frame,GEM_FRAME_BYTES);
        }
        m->tx[0].status|=0x80000000;
        f->regs[TSR/4]|=0x21;
        if (f->fault==NO_RX || (echo && f->fault==NO_ECHO)) { return true; }
        m->rx[0].address|=1;
        m->rx[0].status=0xc000|(f->fault==SHORT_FRAME ? (f->speed ? 41 : bytes-1) : bytes);
        f->regs[RSR/4]|=2;
        if (f->fault==BAD_FRAME) { m->received[42]^=1; }
        if (f->fault==BAD_GUARD) { m->after[0]=0; }
    }
    return true;
}

static uint64_t ticks(void *context) { return ++((struct fixture *)context)->clock; }
static void barrier(void *context) { (void)context; }

static void reset(struct fixture *f)
{
    memset(f,0,sizeof(*f));
    f->regs[MID/4]=0x70109;
    f->regs[DCFG6/4]=0x80000001;
    f->regs[IMR/4]=0x07ffffff;
    f->regs[CFG/4]=0x00280001;
    f->regs[DMA/4]=0x40020784;
    f->regs[TBQP/4]=0x4004;
    f->regs[RBQP/4]=0x5000;
    f->regs[TSR/4]=1;
    f->regs[SA1_LOW/4]=0x34231200;
    f->regs[SA1_HIGH/4]=0x5645;
}

int main(void)
{
    struct fixture f;
    struct rp1_eth_loop_report report;
    const struct rp1_dma_io io={&f,read,write,ticks,barrier};
    enum rp1_eth_loop_result expected[]={GEM_LOOP_OK,GEM_LOOP_SETUP,GEM_LOOP_FRAME,
        GEM_LOOP_FRAME,GEM_LOOP_FRAME,GEM_LOOP_RETAIN,GEM_LOOP_RETAIN};
    for (enum fault fault=NONE;fault<=NO_RESTORE;++fault) {
        reset(&f);
        f.fault=fault;
        uint32_t original[sizeof(f.regs)/sizeof(f.regs[0])];
        memcpy(original,f.regs,sizeof(original));
        assert(rp1_eth_exchange(&io,&f.memory,BUS,8,0,&report)==expected[fault]);
        assert(f.regs[NCR/4]==0 && f.started==(unsigned)(fault!=NO_LOOP_BIT));
        if (fault<NO_RX) { assert(!memcmp(original,f.regs,sizeof(original))); }
        if (fault==NO_LOOP_BIT) {
            assert(report.register_failed && report.offset==NCR && report.expected==2 && report.observed==0);
        }
        if (fault==NO_RESTORE) {
            assert(report.register_failed && report.offset==TBQP && report.expected==original[TBQP/4]);
        }
        if (fault==NO_RX) {
            assert(f.clock>=8 && f.regs[RBQP/4]!=original[RBQP/4]);
            assert(report.tx&0x80000000 && report.rx==0);
        }
        if (fault==NONE) {
            assert(rp1_eth_exchange(&io,&f.memory,BUS,8,0,&report)==GEM_LOOP_OK);
            assert(f.started==2 && !memcmp(original,f.regs,sizeof(original)));
        }
    }
    reset(&f);
    f.regs[DCFG6/4]|=2;
    assert(rp1_eth_exchange(&io,&f.memory,BUS,8,0,&report)==GEM_LOOP_BUSY && f.writes==0);
    reset(&f);
    f.regs[IMR/4]=0;
    assert(rp1_eth_exchange(&io,&f.memory,BUS,8,0,&report)==GEM_LOOP_BUSY && f.writes==0);
    reset(&f);
    f.regs[TSR/4]|=8;
    assert(rp1_eth_exchange(&io,&f.memory,BUS,8,0,&report)==GEM_LOOP_BUSY && f.writes==0);
    reset(&f);
    assert(rp1_eth_exchange(&io,&f.memory,BUS+4,8,0,&report)==GEM_LOOP_BUSY && f.writes==0);
    assert(rp1_eth_exchange(&io,&f.memory,0xffffffc0,8,0,&report)==GEM_LOOP_BUSY && f.writes==0);
    puts("PASS: GEM DMA ownership, repeatable loopback, frame and guard checks, refusal and cleanup.");

    for (unsigned speed=10;speed<=1000;speed*=10) {
        reset(&f);
        f.regs[DCFG6/4]=0x08820000;
        f.regs[IMR/4]=0x3fffffff;
        f.regs[TSR/4]=0;
        f.speed=speed;
        uint32_t original[sizeof(f.regs)/sizeof(f.regs[0])];
        memcpy(original,f.regs,sizeof(original));
        f.fault=DROP_FILTER;
        assert(rp1_eth_exchange(&io,&f.memory,BUS,8,speed,&report)==GEM_LOOP_SETUP && !f.started);
        assert(report.register_failed && report.offset==SA1_HIGH &&
               report.expected==0x218 && report.observed==original[SA1_HIGH/4]);
        assert(!memcmp(original,f.regs,sizeof(original)));
        f.fault=NONE;
        for (unsigned corrupt=0;corrupt<=42;++corrupt) {
            f.corrupt=corrupt;
            enum rp1_eth_loop_result result=rp1_eth_exchange(&io,&f.memory,BUS,8,speed,&report);
            assert(result==(corrupt ? GEM_LOOP_FRAME : GEM_LOOP_OK));
            assert(report.peer_mac==(corrupt ? 0 : 0x020304050607ULL));
            assert(!memcmp(original,f.regs,sizeof(original)));
        }
        f.corrupt=0;
        f.fault=SHORT_FRAME;
        assert(rp1_eth_exchange(&io,&f.memory,BUS,8,speed,&report)==GEM_LOOP_FRAME && !report.peer_mac);
        assert(!memcmp(original,f.regs,sizeof(original)));
        f.fault=NO_RX;
        assert(rp1_eth_exchange(&io,&f.memory,BUS,8,speed,&report)==GEM_LOOP_RETAIN);
        assert(f.regs[NCR/4]==0 && f.regs[RBQP/4]!=original[RBQP/4]);
    }
    puts("PASS: wire ARP request, 10/100/1000 RGMII modes, exact reply matching, restoration and timeout retention.");

    reset(&f);
    assert(rp1_eth_ping(&io,&f.memory,BUS,8,0,&report)==GEM_LOOP_BUSY && !f.writes);
    for (enum fault fault=SHORT_ECHO;fault<=NO_ECHO;++fault) {
        reset(&f);
        f.speed=100;
        f.fault=fault;
        uint32_t original[sizeof(f.regs)/sizeof(f.regs[0])];
        memcpy(original,f.regs,sizeof(original));
        assert(rp1_eth_ping(&io,&f.memory,BUS,8,100,&report)==
               (fault==NO_ECHO ? GEM_LOOP_RETAIN : GEM_LOOP_FRAME));
        assert(report.echo && f.started==2 && !f.regs[NCR/4]);
        if (fault!=NO_ECHO) { assert(!memcmp(original,f.regs,sizeof(original))); }
    }
    reset(&f);
    f.speed=100;
    uint32_t original[sizeof(f.regs)/sizeof(f.regs[0])];
    memcpy(original,f.regs,sizeof(original));
    assert(rp1_eth_ping(&io,&f.memory,BUS,8,100,&report)==GEM_LOOP_OK);
    assert(report.echo && report.peer_mac==0x020304050607ULL && f.started==2);
    assert(!memcmp(original,f.regs,sizeof(original)));
    f.started=0;
    f.corrupt=1;
    assert(rp1_eth_ping(&io,&f.memory,BUS,8,100,&report)==GEM_LOOP_FRAME && !report.echo && f.started==1);
    puts("PASS: ARP then IPv4 ICMP echo, wire checksums, correlated reply, malformed replies and DMA cleanup.");
}
