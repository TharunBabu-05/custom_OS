#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "host_assert.h"
#include "brcm_control.h"
/* Independent native ABI layout for the stable station-info prefix. */
struct reference_info { uint16_t ver,len,cap;uint32_t flags,idle;unsigned char peer[6];uint32_t count;unsigned char rates[16];uint32_t in,listen,tx,fail,rxu,rxm,txrate,rxrate,decok,decfail; };
_Static_assert(offsetof(struct reference_info,flags)==8 && offsetof(struct reference_info,peer)==16 && offsetof(struct reference_info,tx)==52 && offsetof(struct reference_info,decfail)==80,"station ABI");
static const unsigned char peer[6]={2,0,0,0,0,2};
static struct brcm_control_state s;
static struct { unsigned calls,fail_at,writes,position,bytes;bool pending,reject,cancel,cancel_rx;unsigned char reply[600];unsigned command; } m;
static unsigned get32(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static void put32(unsigned char *p,unsigned x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(8*i)); } }
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static enum block_result rd(void *ctx,uint32_t at,uint32_t *v) { assert(ctx==&m);*v=at==0x18004020 && m.pending ? 0x40 : 0;return touch(); }
static enum block_result wr(void *ctx,uint32_t at,uint32_t v) { assert(ctx==&m && at==0x18004020 && v==0x40);return touch(); }
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000);return touch(); }
static void fixture(unsigned char *out)
{
    struct reference_info value={.ver=7,.len=280,.flags=0x4038,.tx=11,.fail=2,.rxu=7,.rxm=3,.decok=10,.decfail=1};memcpy(value.peer,peer,6);memset(out,0,512);memcpy(out,&value,sizeof(value));
}
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    assert(ctx==&m);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }unsigned char *p=buffer;
    if (write) {
        assert(!(n&3) && n>=36);struct brcm_sdpcm_header h;assert(brcm_sdpcm_header(p,n,&h)==BLOCK_OK && !h.channel && h.offset==12 && h.sequence==s.tx_seq);
        unsigned command=get32(p+12),flags=get32(p+20),payload=get32(p+16),length=28;m.command=command;++m.writes;
        if (command==121 || command==122) { assert((flags&2) && payload==6 && h.length==34 && !memcmp(p+28,peer,6) && !p[34] && !p[35]); }
        else { assert(command==262 && !(flags&2) && payload==521 && h.length==549 && !memcmp(p+28,"sta_info",9) && !memcmp(p+37,peer,6));length+=512; }
        memset(m.reply,0,sizeof(m.reply));put32(m.reply,((unsigned)(uint16_t)~length<<16)|length);m.reply[4]=s.rx_seq;m.reply[7]=12;m.reply[9]=(unsigned char)(s.tx_seq+4);
        put32(m.reply+12,command);put32(m.reply+16,command==262 ? 512 : 6);put32(m.reply+20,(flags&0xffff0000)|(m.reject ? 1 : flags&2));put32(m.reply+24,m.reject ? 0xffffffe9u : 0);
        if (command==262) { fixture(m.reply+28); }m.position=0;m.bytes=(length+3)&~3u;m.pending=true;return BLOCK_OK;
    }
    assert(m.pending && n<=512);memset(p,0,n);unsigned available=m.bytes-m.position;if (available>n) { available=(unsigned)n; }memcpy(p,m.reply+m.position,available);m.position+=available;
    if (m.position==m.bytes) { m.pending=false; }if (m.cancel_rx) { m.cancel=true; }return BLOCK_OK;
}
static const struct brcm_control_io io={.context=&m,.read32=rd,.write32=wr,.packet=packet,.delay_us=delay};
static bool guard(void *ctx) { assert(ctx==&m);return !m.cancel; }
static void seed(void) { memset(&s,0,sizeof(s));memset(&m,0,sizeof(m));s.ready=s.f2_ready=s.device_ready=true;s.tx_seq=7;s.tx_max=11; }
int main(void)
{
    struct brcm_station_info out;uint32_t status=0;seed();assert(brcm_control_station_info(&io,&s,peer,&out,&status)==BLOCK_OK && out.valid && out.version==7 && out.length==280 && out.flags==0x4038 && out.statistics && out.tx_packets==11 && out.tx_failures==2 && out.rx_unicast==7 && out.rx_multicast==3 && out.decrypt_ok==10 && out.decrypt_fail==1);
    unsigned total=m.calls;for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(brcm_control_station_info(&io,&s,peer,&out,&status)==BLOCK_IO && !out.valid && !s.ready); }
    for (unsigned auth=0;auth<2;++auth) {
        seed();assert(brcm_control_port(&io,&s,peer,auth,&status,guard,&m)==BLOCK_OK && m.command==(auth ? 121u : 122u) && m.writes==1 && !status && s.ready);
        total=m.calls;for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(brcm_control_port(&io,&s,peer,auth,&status,guard,&m)==BLOCK_IO && !s.ready); }
        seed();m.reject=true;assert(brcm_control_port(&io,&s,peer,auth,&status,guard,&m)==BLOCK_UNSUPPORTED && status==0xffffffe9u && s.ready);
        seed();m.cancel=true;assert(brcm_control_port(&io,&s,peer,auth,&status,guard,&m)==BLOCK_NOT_READY && !m.writes && s.ready);
    }
    seed();m.pending=m.cancel_rx=true;m.bytes=12;put32(m.reply,0xfff3000c);m.reply[7]=12;m.reply[9]=11;
    assert(brcm_control_port(&io,&s,peer,true,&status,guard,&m)==BLOCK_NOT_READY && m.cancel && !m.writes && s.ready);
    unsigned char data[512];fixture(data);
    for (size_t n=0;n<280;++n) { assert(brcm_station_info_decode(data,n,peer,&out)!=BLOCK_OK && !out.valid); }
    for (unsigned version=0;version<10;++version) { fixture(data);data[0]=(unsigned char)version;assert((brcm_station_info_decode(data,512,peer,&out)==BLOCK_OK)==(version>=3 && version<=7)); }
    fixture(data);data[16]^=1;assert(brcm_station_info_decode(data,512,peer,&out)==BLOCK_UNSUPPORTED && !out.valid);
    fixture(data);data[2]=1;data[3]=3;assert(brcm_station_info_decode(data,512,peer,&out)==BLOCK_RANGE && !out.valid);
    fixture(data);data[2]=22;data[3]=0;assert(brcm_station_info_decode(data,512,peer,&out)==BLOCK_OK && out.valid && !out.statistics);
    fixture(data);data[9]&=(unsigned char)~0x40;assert(brcm_station_info_decode(data,512,peer,&out)==BLOCK_OK && !out.statistics);
    const unsigned char multicast[6]={1,0,0,0,0,2};seed();assert(brcm_control_port(&io,&s,multicast,true,&status,guard,&m)==BLOCK_ARGUMENT && !m.calls);
    puts("PASS: independent station-info ABI, actual peer-bound sta_info CDC query and guarded 121/122 SETs, SET length echo, SCB stats length/flag gates, all truncations/versions, wrong peer, credit-time guard cancellation, rejection and every bus-I/O failure; no arbitrary command API");return 0;
}
