#include <stdio.h>
#include <string.h>
#include "brcm_control.h"
#include "host_assert.h"
struct mock {
    unsigned mode,calls,fail_at,writes,head,tail,position,sequence,tx,delays;
    uint8_t enable,clock,low,high;
    bool mailbox_irq;
    unsigned char frames[72][512];unsigned lengths[72];
};
static struct mock m;
static struct brcm_control_state state;
static const struct brcm_start_state boot={.phase=START_READY,.error=BLOCK_OK,.ready=true};
static const char version[]="Cypress test firmware: host-side fixture only, never a measured chip version. This deliberately spans more than the first 64 bytes of an SDPCM packet.";
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned char *frame(unsigned length,unsigned channel)
{
    assert(m.tail<72 && length<=512);unsigned char *p=m.frames[m.tail];m.lengths[m.tail++]=(length+3)&~3u;
    memset(p,0,512);put32(p,((uint32_t)(uint16_t)~length<<16)|length);
    p[4]=(unsigned char)m.sequence++;p[5]=(unsigned char)channel;p[7]=12;p[9]=4;return p;
}
static enum block_result read8(void *ctx,unsigned fn,uint32_t at,uint8_t *v)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }
    if (fn==1) { assert(at==0x1000e);*v=m.mode==3 ? 0x80 : m.mode==26 ? 255 : m.clock; }
    else { assert(!fn);if (at==2) { *v=m.enable; }else if (at==3) { *v=m.mode==2 ? 2 : m.enable; }
        else if (at==0x210) { *v=m.low; }else { assert(at==0x211);*v=m.mode==1 ? 3 : m.high; }
        if (m.mode==33 && at==3 && m.delays<2) { *v=2; } }
    return BLOCK_OK;
}
static enum block_result write8(void *ctx,unsigned fn,uint32_t at,uint8_t v)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }++m.writes;
    if (fn==1) { if (at==0x1000e) { assert(v==0x12);m.clock=m.mode==4 ? 0xd0 : 0xd2; }else { assert(at==0x10008 && v==8); } }
    else {
        assert(!fn);if (at==0x210) { assert(v==0);m.low=v; }else if (at==0x211) { assert(v==2);m.high=v; }
        else { assert(at==2 && v==6 && m.low==0 && m.high==2);m.enable=v;m.mailbox_irq=true;
            if (m.mode!=8) {
                if (m.mode==22 || m.mode==23) { frame(100,m.mode==22 ? 1 : 2); }
                unsigned char *p=frame(12,0);if (m.mode==9) { p[3]^=1; }if (m.mode==10) { p[7]=11; }
                if (m.mode==11) { p[5]=3; }if (m.mode==16) { put32(p,0xeffe1001); }
                if (m.mode==20) { p[9]=100; }if (m.mode==31) { put32(p,0xfff2000d); }
                if (m.mode==32) { p[4]=7; }
                if (m.mode==27) { for (unsigned i=0;i<65;++i) { frame(12,0); } }
            }
        }
    }return BLOCK_OK;
}
static enum block_result read32(void *ctx,uint32_t at,uint32_t *v)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }
    if (at==0x18004020) { *v=(m.mailbox_irq ? 0x80 : 0)|(m.head<m.tail ? 0x40 : 0); }
    else { assert(at==0x1800404c);*v=m.mode==5 || (m.mode==33 && m.delays<4) ? 0 : m.mode==6 ? 0x40010 : m.mode==7 ? 0x5000a : 0x4000a; }
    return BLOCK_OK;
}
static enum block_result write32(void *ctx,uint32_t at,uint32_t v)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }++m.writes;
    if (at==0x18004048) { assert(v==0x40000 && m.enable==2); }
    else if (at==0x18004024) { assert(v==0xf0 && m.enable==6); }
    else if (at==0x18004040) { assert(v==2);m.mailbox_irq=false; }
    else { assert(at==0x18004020 && v && !(v&~0xf0u)); }
    return BLOCK_OK;
}
static void reply(uint32_t command,uint32_t id)
{
    unsigned payload=command==1 ? 4 : m.mode==18 ? 256 : (unsigned)sizeof(version);
    unsigned char *p=frame(28+payload,0);put32(p+12,m.mode==13 ? command+1 : command);
    put32(p+16,m.mode==15 ? 512 : payload);put32(p+20,id<<16|(m.mode==12 ? 1u : m.mode==28 ? 2u : m.mode==29 ? 0x1000u : 0));
    put32(p+24,m.mode==12 || m.mode==30 ? 0xffffffe9u : 0);
    if (command==1) { put32(p+28,m.mode==17 ? 3 : 2); }
    else {
        if (m.mode==18) { memset(p+28,'x',256); }else { memcpy(p+28,version,sizeof(version)); }
        if (m.mode==19) { p[28]=27; }if (m.mode==21) { p[28]=0; }
    }
}
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }
    assert(m.enable==6 && n && !(n&3) && n<=512);
    unsigned char *p=buffer;
    if (write) {
        ++m.writes;struct brcm_sdpcm_header h;assert(brcm_sdpcm_header(p,n,&h)==BLOCK_OK && h.offset==12 && !h.channel && h.sequence==m.tx++);
        uint32_t command=get32(p+12),id=get32(p+20)>>16;assert(get32(p+20)==id<<16 && !get32(p+24));
        assert((command==1 && n==32 && get32(p+16)==4) || (command==262 && n==284 && get32(p+16)==256 && !strcmp((char *)p+28,"ver")));
        if (m.mode==14) { reply(command,id+1); }
        else { if (m.mode==24) { reply(command,id+1); }reply(command,id); }
    } else {
        memset(p,0,n);
        if (m.head<m.tail) {
            unsigned remain=m.lengths[m.head]-m.position;
            assert((!m.position && n==64) || (m.position && n==remain));
            unsigned amount=remain<n ? remain : (unsigned)n;memcpy(p,m.frames[m.head]+m.position,amount);m.position+=amount;
            if (m.position==m.lengths[m.head]) { ++m.head;m.position=0; }
        } else { assert(n==64); }
    }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ (void)ctx;assert(us==10000);++m.delays;return touch(); }
static void seed(void) { memset(&m,0,sizeof(m));memset(&state,0,sizeof(state));m.clock=0xd0;m.enable=2; }
int main(void)
{
    struct brcm_control_io io={NULL,read8,write8,read32,write32,packet,delay};
    seed();assert(brcm_control_start(&io,&boot,&state)==BLOCK_OK && state.ready && state.phase==CONTROL_READY);
    assert(state.ioctl_version==2 && !strcmp(state.version,version) && state.tx_frames==2 && state.rx_frames==3 && state.block_size==512 && state.device_ready);
    unsigned total=m.calls,calls=m.calls;assert(brcm_control_start(&io,&boot,&state)==BLOCK_OK && m.calls==calls);
    for (unsigned mode=1;mode<=32;++mode) {
        seed();m.mode=mode;if (mode==25) { m.enable=0; }
        enum block_result r=brcm_control_start(&io,&boot,&state);
        if (mode==22 || mode==23 || mode==24 || mode==32) { assert(r==BLOCK_OK && state.ready); }
        else { if (r==BLOCK_OK) { fprintf(stderr,"unexpected success mode=%u\n",mode); }assert(r!=BLOCK_OK && !state.ready); }
        if (mode==22) { assert(state.event_frames==1); }if (mode==23) { assert(state.data_frames==1); }
        if (mode==24) { assert(state.stale_replies==2); }if (mode==32) { assert(state.seq_resync>0); }
        if (mode==2 || mode==5 || mode==8 || mode==14) { assert(r==BLOCK_TIMEOUT); }
        if (mode==3 || mode==25 || mode==26) { assert(!m.writes && !state.attempted); }
        calls=m.calls;if (state.attempted) { assert(brcm_control_start(&io,&boot,&state)==r && m.calls==calls); }
    }
    for (unsigned fail=1;fail<=total;++fail) {
        seed();m.fail_at=fail;assert(brcm_control_start(&io,&boot,&state)==BLOCK_IO && !state.ready && m.calls==fail);
        calls=m.calls;if (state.attempted) { assert(brcm_control_start(&io,&boot,&state)==BLOCK_IO && m.calls==calls); }
    }
    seed();m.mode=33;assert(brcm_control_start(&io,&boot,&state)==BLOCK_OK && state.ready && m.delays>=4);
    seed();struct brcm_start_state bad=boot;bad.ready=false;
    assert(brcm_control_start(&io,&bad,&state)==BLOCK_NOT_READY && !m.calls);
    assert(brcm_control_start(NULL,&boot,&state)==BLOCK_ARGUMENT);
    unsigned char header[12]={0};struct brcm_sdpcm_header h;
    assert(brcm_sdpcm_header(header,sizeof(header),&h)==BLOCK_NOT_READY);
    assert(brcm_sdpcm_header(header,11,&h)==BLOCK_ARGUMENT);
    /* Mutate all header bytes with a short input; parsing must remain bounded. */
    for (unsigned i=0;i<12;++i) { for (unsigned v=0;v<256;++v) { memset(header,0,12);header[i]=(unsigned char)v;(void)brcm_sdpcm_header(header,12,&h); } }
    printf("PASS: F2 setup/mailbox/credits, SDPCM/BCDC version replies, partial RX, event/data drain, stale/sequence handling; %u per-operation I/O failures and 28 negative states\n",total);
    puts("PASS: no writes before verified startup/live prechecks, no SET commands, malformed lengths/channels/IDs/flags/status/version strings rejected, bounded polling and one-shot refusal");
    return 0;
}
