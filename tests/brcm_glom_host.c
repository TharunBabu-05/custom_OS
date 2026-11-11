/* Synthetic descriptors/superframes: only the captured 12-byte descriptor
 * header is a physical fixture; its unrecorded length list is not invented
 * as hardware evidence. Exercise real control FIFO parsing and callbacks. */
#include <stdio.h>
#include <string.h>
#include "brcm_control.h"
#include "host_assert.h"
static struct brcm_control_state state;
static struct {
    unsigned char frames[8][BRCM_GLOM_MAX];unsigned lengths[8],head,tail,position,calls,fail_at,reads,writes,events,data,order;
    bool callback,aggregate_on_write,aggregates[8],fuzz;unsigned char seen[117];
} m;
static void le16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void le32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void header(unsigned char *p,unsigned len,unsigned seq,unsigned channel,unsigned offset)
{ le16(p,len);le16(p+2,(uint16_t)~len);p[4]=(unsigned char)seq;p[5]=(unsigned char)channel;p[7]=(unsigned char)offset;p[9]=104; }
static enum block_result touch(void) { assert(!m.callback);return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static enum block_result rd(void *ctx,uint32_t at,uint32_t *v)
{ assert(ctx==&m && (at==0x18004020 || at==0x1800404c));enum block_result r=touch();*v=at==0x18004020 ? (m.head<m.tail ? 0x40 : 0) : 0x40002;return r; }
static enum block_result wr(void *ctx,uint32_t at,uint32_t v)
{ assert(ctx==&m && at==0x18004020 && v==0x40);return touch(); }
static void enqueue(void)
{
    assert(m.tail+2<=8);unsigned char *desc=m.frames[m.tail];memset(desc,0,BRCM_GLOM_MAX);
    header(desc,16,83,0x83,12);desc[6]=0x18;le16(desc+12,100);le16(desc+14,132);m.lengths[m.tail++]=16;
    unsigned char *super=m.frames[m.tail];memset(super,0,BRCM_GLOM_MAX);header(super,232,84,3,12);
    unsigned char *event=super+12;header(event,88,84,1,12);event[12]=0x20;event[13]=0x51;event[87]=0x91;
    unsigned char *data=super+100;header(data,129,85,2,12);data[12]=0x20;
    for (unsigned i=1;i<117;++i) { data[12+i]=(unsigned char)i; }m.aggregates[m.tail]=true;m.lengths[m.tail++]=512;
}
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    assert(ctx==&m && n && n<=512 && !(n&3));enum block_result r=touch();if (r!=BLOCK_OK) { return r; }
    unsigned char *p=buffer;
    if (write) {
        ++m.writes;assert(get32(p+12)==3 && (get32(p+20)&2));uint32_t id=get32(p+20)&0xffff0000u;
        if (m.aggregate_on_write) { enqueue(); }
        assert(m.tail<8);unsigned char *reply=m.frames[m.tail];memset(reply,0,BRCM_GLOM_MAX);
        header(reply,28,86,0,12);le32(reply+12,3);le32(reply+20,id);m.lengths[m.tail++]=28;
    }else {
        ++m.reads;memset(p,0,n);if (m.head==m.tail) { return n==64 ? BLOCK_OK : BLOCK_IO; }
        unsigned remaining=m.lengths[m.head]-m.position;
        bool aggregate=m.aggregates[m.head];
        assert(aggregate ? n==512 : !m.position ? n==64 : n==(remaining>512 ? 512 : remaining));
        unsigned take=remaining<n ? remaining : (unsigned)n;memcpy(p,m.frames[m.head]+m.position,take);m.position+=take;
        if (m.position==m.lengths[m.head]) { ++m.head;m.position=0; }
    }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000);return touch(); }
static const struct brcm_control_io io={.context=&m,.read32=rd,.write32=wr,.packet=packet,.delay_us=delay};
static void event(void *ctx,const void *buffer,size_t n)
{ assert(ctx==&m && !m.callback);if (m.fuzz) { assert(buffer && n<=4096);++m.events;return; }assert(m.order==0);m.callback=true;const unsigned char *p=buffer;assert(n==76 && p[0]==0x20 && p[1]==0x51 && p[75]==0x91);++m.events;++m.order;m.callback=false; }
static void data(void *ctx,const void *buffer,size_t n)
{ assert(ctx==&m && !m.callback);if (m.fuzz) { assert(buffer && n<=4096);++m.data;return; }assert(m.order==1);m.callback=true;assert(n==117);memcpy(m.seen,buffer,n);assert(m.seen[0]==0x20);for (unsigned i=1;i<117;++i) { assert(m.seen[i]==(unsigned char)i); }++m.data;++m.order;m.callback=false; }
static void seed(void)
{
    memset(&m,0,sizeof(m));state=(struct brcm_control_state){.ready=true,.f2_ready=true,.device_ready=true,.tx_seq=64,.tx_max=104,.rx_seq=83,.rx_limit=512,
        .event=event,.event_context=&m,.data_observer=data,.data_context=&m};
}
static enum block_result run(void)
{ enum block_result r=brcm_control_poll(&io,&state);return r==BLOCK_OK ? brcm_control_poll(&io,&state) : r; }
int main(void)
{
    seed();enqueue();assert(run()==BLOCK_OK && state.ready && state.glom_descriptors==1 && state.glom_frames==1 && state.glom_subframes==2 && m.events==1 && m.data==1 && state.rx_seq==86);
    assert(state.glom_phase==5 && !state.glom_count && state.rx_frames==4 && m.reads==2 && m.head==m.tail);unsigned total=m.calls;
    /* Captured descriptor header at the exact pre-existing TX/RX boundary. */
    assert(state.last_header[0]==0xffef0010 && state.last_header[1]==0x0c188353 && state.last_header[2]==0x6800);
    for (unsigned fail=1;fail<=total;++fail) {
        seed();enqueue();m.fail_at=fail;assert(run()!=BLOCK_OK && !state.ready && !m.events && !m.data);
        unsigned calls=m.calls;assert(brcm_control_poll(&io,&state)==BLOCK_NOT_READY && m.calls==calls);
    }
    /* Reject malformed second frame before dispatch of a valid first one. */
    for (unsigned mode=0;mode<17;++mode) {
        seed();enqueue();unsigned char *desc=m.frames[0],*super=m.frames[1],*second=super+100;
        switch (mode) {
        case 0: le16(desc+12,23);break;
        case 1: le16(desc,15);le16(desc+2,(uint16_t)~15);break;
        case 2: le16(desc+14,4097);break;
        case 3: le16(desc,46);le16(desc+2,(uint16_t)~46);m.lengths[0]=48;break;
        case 4: super[2]^=1;break;
        case 5: super[5]=0x83;break;
        case 6: super[5]=2;break;
        case 7: super[7]=99;break;
        case 8: le16(super,513);le16(super+2,(uint16_t)~513);break;
        case 9: second[2]^=1;break;
        case 10: second[5]=0;break;
        case 11: second[5]=0x83;break;
        case 12: second[4]=87;break;
        case 13: second[7]=11;break;
        case 14: le16(second,133);le16(second+2,(uint16_t)~133);break;
        case 15: state.rx_limit=3;break;
        case 16: super[9]=200;break;
        }
        assert(run()!=BLOCK_OK && !state.ready && !m.events && !m.data && state.glom_failures==1);
    }
    /* Bigger padding/read boundaries, offset extension and sequence wrap. */
    seed();enqueue();unsigned char *desc=m.frames[0],*super=m.frames[1];
    memmove(super+512,super+100,132);memset(super+100,0,412);le16(desc+12,512);le16(desc+14,132);header(super,644,84,3,12);m.lengths[1]=1024;
    assert(run()==BLOCK_OK && m.events==1 && m.data==1 && m.reads==3 && state.glom_wire_bytes==1024);
    seed();enqueue();desc=m.frames[0];super=m.frames[1];desc[4]=254;super[4]=255;super[16]=255;super[104]=0;state.rx_seq=254;
    assert(run()==BLOCK_OK && state.rx_seq==1 && !state.seq_resync);
    seed();enqueue();desc=m.frames[0];super=m.frames[1];memmove(super+104,super+100,132);memmove(super+16,super+12,88);super[7]=16;
    le16(desc+12,104);le16(super,236);le16(super+2,(uint16_t)~236);
    assert(run()==BLOCK_OK && m.events==1 && m.data==1);
    /* Ordinary CDC ACK after aggregate while a DOWN command is pending. */
    seed();m.aggregate_on_write=true;uint32_t status;assert(brcm_control_radio(&io,&state,false,&status)==BLOCK_OK && !status && m.writes==1 && m.events==1 && m.data==1 && state.ready);
    /* The profile accepts a single subframe and up to sixteen, including
     * empty frames, and exactly 8 KiB of block-rounded wire data. */
    seed();enqueue();desc=m.frames[0];super=m.frames[1];header(desc,14,83,0x83,12);le16(desc+12,100);m.lengths[0]=16;header(super,100,84,3,12);
    assert(run()==BLOCK_OK && m.events==1 && !m.data && state.glom_subframes==1);
    seed();enqueue();desc=m.frames[0];super=m.frames[1];memset(desc,0,BRCM_GLOM_MAX);memset(super,0,BRCM_GLOM_MAX);
    header(desc,44,83,0x83,12);m.lengths[0]=44;unsigned at=12;
    for (unsigned i=0;i<16;++i) { le16(desc+12+2*i,i ? 12 : 24);header(super+at,12,84+i,i&1 ? 2 : 1,12);at+=12; }
    header(super,204,84,3,12);assert(run()==BLOCK_OK && state.glom_subframes==16 && !m.events && !m.data && state.rx_seq==100);
    seed();enqueue();desc=m.frames[0];super=m.frames[1];memset(super,0,BRCM_GLOM_MAX);
    le16(desc+12,4096);le16(desc+14,4096);header(super,8192,84,3,12);header(super+12,12,84,1,12);header(super+4096,12,85,2,12);m.lengths[1]=8192;
    assert(run()==BLOCK_OK && state.glom_wire_bytes==8192 && m.reads==17 && !m.events && !m.data);
    seed();enqueue();desc=m.frames[0];le16(desc,18);le16(desc+2,(uint16_t)~18);le16(desc+12,4096);le16(desc+14,4096);le16(desc+16,12);m.lengths[0]=20;
    assert(run()==BLOCK_RANGE && state.glom_failures==1 && !m.events && !m.data);
    /* Pure header mutation never trusts raw flags as a channel. */
    unsigned char raw[12]={0};struct brcm_sdpcm_header parsed;uint32_t random=0x83;
    for (unsigned i=0;i<10000;++i) {
        random=random*1664525u+1013904223u;for (unsigned j=0;j<12;++j) { raw[j]=(unsigned char)(random>>(8*(j%4))); }
        if (brcm_sdpcm_header(raw,sizeof(raw),&parsed)==BLOCK_OK) { assert(parsed.channel<=3 && (!parsed.descriptor || parsed.channel==3) && parsed.offset>=12 && parsed.offset<=parsed.length && parsed.length<=4096); }
    }
    for (unsigned i=0;i<5000;++i) {
        seed();enqueue();m.fuzz=true;random=random*1664525u+1013904223u;unsigned which=random%40;
        unsigned char *field=which<4 ? m.frames[0]+12+which : which<16 ? m.frames[1]+which-4 : which<28 ? m.frames[1]+12+which-16 : m.frames[1]+100+which-28;
        *field^=(unsigned char)(random>>24);enum block_result r=run();
        if (r!=BLOCK_OK) { assert(!m.events && !m.data && !state.ready); }
        else { assert(m.events+m.data<=2 && state.ready); }
    }
    puts("PASS: captured SDPCM 0x83 descriptor header replay, two-pass aggregate validation, event/data ordering, split 512-byte FIFO reads, padding/offset/sequence wrap, pending CDC ACK, injected I/O failures and 17 malformed/limit cases without partial dispatch");
    puts("PASS: one/sixteen-subframe and 8-KiB boundaries, empty-frame skipping, overflow rejection, 10000 header and 5000 descriptor/aggregate/subframe mutations with no callbacks on rejected aggregates");return 0;
}
