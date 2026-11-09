#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_power.h"
#include "dt_property.h"
#include "host_assert.h"
#define HOST 0x1001100000ULL
#define GPIO 0x107d508500ULL
#define MUX 0x107d504100ULL
struct model {
    uint32_t host[0x600/4],gpio[0x40/4],mux[0x30/4],stepping;
    unsigned reads,writes,mode; uint64_t time;
};
static uint32_t *reg(struct model *m,uint64_t address)
{
    assert(!(address&3));
    if (address==0x1001504004ULL) { return &m->stepping; }
    if (address>=HOST && address<HOST+sizeof(m->host)) { return &m->host[(address-HOST)/4]; }
    if (address>=GPIO && address<GPIO+sizeof(m->gpio)) { return &m->gpio[(address-GPIO)/4]; }
    if (address>=MUX && address<MUX+sizeof(m->mux)) { return &m->mux[(address-MUX)/4]; }
    assert(!"Unexpected MMIO address"); return NULL;
}
static bool rd(void *context,uint64_t address,uint32_t *value)
{ struct model *m=context; ++m->reads; *value=*reg(m,address); return true; }
static uint32_t mux_mask(bool d0,uint64_t address)
{
    if (address==MUX+(d0 ? 8 : 12)) { return 0xff0f0000; }
    if (address==MUX+(d0 ? 12 : 16)) { return 0xffff; }
    if (address==MUX+(d0 ? 20 : 36)) { return d0 ? 0x3f300000 : 0x03ffcc00; }
    if (d0 && address==MUX+24) { return 0x3f; }
    assert(!"Unexpected pinmux/pad write"); return 0;
}
static bool wr(void *context,uint64_t address,uint32_t value)
{
    struct model *m=context; ++m->writes; uint32_t *p=reg(m,address),mask=0;
    if (address>=MUX && address<MUX+sizeof(m->mux)) { mask=mux_mask(m->stepping==0x27120030,address); }
    else if (address==GPIO+4) { mask=1u<<28; }
    else if (address==GPIO+8) { mask=(1u<<28)|(3u<<30); }
    else if (address==GPIO+0x28) { mask=15; }
    else if (address==HOST+0x28) { mask=0xf3e; }
    else if (address==HOST+0x400) { mask=3u<<30; }
    else if (address==HOST+0x444) { mask=3; }
    else if (address==HOST+0x38) { assert(value==0); mask=UINT32_MAX; }
    else if (address==HOST+0x2c) {
        if (value&(1u<<24)) { assert(value==(1u<<24)); *p=m->mode==1 ? value : 0; return true; }
        assert((value&~7u)==(14u<<16) || (value&~7u)==((14u<<16)|(250u<<8)));
        *p=value;
        if ((value&1) && m->mode!=2) { *p|=2; } return true;
    } else { assert(!"Unexpected write outside wireless resources"); }
    assert(!((value^*p)&~mask)); *p=value; return true;
}
static uint64_t ticks(void *context) { return ++((struct model *)context)->time; }
static void seed(struct model *m,bool d0)
{
    memset(m,0,sizeof(*m)); m->stepping=d0 ? 0x27120030 : 0x27120021;
    m->host[0x28/4]=0x00800000; m->host[0x40/4]=0x55eec832;
    m->host[0xfc/4]=0x99030000; m->gpio[8/4]=UINT32_MAX; m->gpio[0x28/4]=UINT32_MAX;
    for (unsigned i=0;i<sizeof(m->mux)/4;++i) { m->mux[i]=0xa5a5a5a5; }
}
int main(void)
{
    FILE *f=fopen(".dist/rpi5-firmware/bcm2712-rpi-5-b.dtb","rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); rewind(f);
    unsigned char *dt=malloc((size_t)n); assert(dt && fread(dt,1,(size_t)n,f)==(size_t)n); fclose(f);
    assert(pi5_wifi_power_description(dt,(size_t)n));
    struct model m; struct wifi_power_io io={rd,wr,ticks,&m,1000}; struct wifi_power_state state;
    for (unsigned d0=0;d0<2;++d0) {
        seed(&m,d0!=0); state=(struct wifi_power_state){0};
        assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_OK);
        assert(state.ready && state.phase==WIFI_POWER_READY && state.sd_hz==400000 && m.time>=160);
        assert(m.host[0x2c/4]==0xefa07 && m.host[0x28/4]==0x00800f00);
        assert((m.gpio[4/4]&(1u<<28)) && !(m.gpio[8/4]&(1u<<28)));
        if (d0) {
            assert((m.mux[8/4]&0xff0f0000)==0x11000000 && (m.mux[12/4]&0xffff)==0x1111);
            assert((m.mux[20/4]&0x3f300000)==0x28000000 && (m.mux[24/4]&63)==42);
        } else {
            assert((m.mux[12/4]&0xff0f0000)==0x44000000 && (m.mux[16/4]&0xffff)==0x3434);
            assert((m.mux[36/4]&0x03ffcc00)==0x02aa0000);
        }
        assert((m.host[0x444/4]&3)==2 && (m.host[0x400/4]>>30)==2);
        unsigned writes=m.writes;
        assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_NOT_READY && m.writes==writes);
    }
    for (unsigned mode=1;mode<=2;++mode) {
        seed(&m,true); m.mode=mode; state=(struct wifi_power_state){0};
        assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_TIMEOUT && state.attempted && !state.ready);
        assert(!(m.gpio[4/4]&(1u<<28)) && !(m.host[0x28/4]&256) && !(m.host[0x2c/4]&5));
    }
    seed(&m,true); m.stepping=0x271200ff; state=(struct wifi_power_state){0};
    assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m,true); m.host[0x40/4]=0; state=(struct wifi_power_state){0};
    assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m,true); m.host[0x2c/4]=7; state=(struct wifi_power_state){0};
    assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m,true); state=(struct wifi_power_state){0};
    assert(pi5_wifi_power_up(&io,dt,(size_t)n-1,&state)==BLOCK_UNSUPPORTED && !m.writes && !m.reads);
    const unsigned char *p; uint32_t length;
    assert(dt_property(dt,(size_t)n,"/wl-on-reg","gpio",&p,&length));
    unsigned char *gpio=dt+(p-dt); gpio[7]=29;
    seed(&m,true); state=(struct wifi_power_state){0};
    assert(pi5_wifi_power_up(&io,dt,(size_t)n,&state)==BLOCK_UNSUPPORTED && !m.writes);
    free(dt);
    puts("PASS: C0/C1 and D0 WLAN pin/pull isolation, GPIO sequencing/delays, SD routing/presence, 400 kHz clock, timeout shutdown, DT/stepping validation and retry refusal");
    return 0;
}
