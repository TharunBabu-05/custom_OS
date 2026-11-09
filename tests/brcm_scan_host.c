#include <stdio.h>
#include <string.h>
#include "brcm_scan.h"
#include "host_assert.h"
static struct brcm_scan_state scan;
static unsigned char event[1024],bss[512];
static struct { unsigned mode,calls,fail_at,sets,ups,downs,polls;uint64_t ms;unsigned char mask[16]; } m;
static void p16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void p32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static void b16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void b32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(24-i*8)); } }
static uint32_t g32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static const unsigned char rsnie[]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,2,0,15,172,8,192,0};
static unsigned make_bss(void)
{
    memset(bss,0,sizeof(bss));unsigned n=126+sizeof(rsnie);p32(bss,109);p32(bss+4,n);
    const unsigned char mac[]={2,17,34,51,68,85};memcpy(bss+8,mac,6);p16(bss+16,16);
    bss[18]=7;memcpy(bss+19,"hotspot",7);p16(bss+72,0x1006);p16(bss+78,(uint16_t)-49);bss[88]=6;
    p16(bss+116,126);p32(bss+120,sizeof(rsnie));memcpy(bss+126,rsnie,sizeof(rsnie));return n;
}
static unsigned make_event(unsigned status,bool data)
{
    unsigned n=data ? make_bss() : 0,bytes=data ? 12+n : 0;memset(event,0,sizeof(event));event[0]=0x20;
    unsigned char *e=event+4;b16(e+12,0x886c);b16(e+14,0x8001);e[20]=16;e[21]=24;b16(e+22,1);b16(e+24,2);
    b32(e+28,69);b32(e+32,status);b32(e+44,bytes);
    if (data) { unsigned char *p=e+72;p32(p,bytes);p32(p+4,109);p16(p+8,0x4f53);p16(p+10,1);memcpy(p+12,bss,n); }
    return 4+72+bytes;
}
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t len,void *out,size_t capacity,size_t *actual,uint32_t *status)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }*actual=0;*status=0;
    if (!set) {
        assert(!input && !len);memset(out,0,capacity);
        if (!strcmp(name,"country")) { assert(capacity==12);memcpy(out,m.mode==3 ? "US\0\0" : "IN\0\0",4);memcpy((unsigned char *)out+8,"IN\0\0",4);*actual=12; }
        else if (!strcmp(name,"scan_ver")) {
            assert(capacity==4);if (m.mode==4) { return BLOCK_IO; }
            if (m.mode==1) { p32(out,2);*actual=4; }else { *status=m.mode==5 ? 0xfffffffcu : 0xffffffe9u;return BLOCK_UNSUPPORTED; }
        } else {
            assert(!strcmp(name,"event_msgs") && capacity==16);memcpy(out,m.mask,16);*actual=m.mode==6 ? 15 : 16;
            if ((m.mode==7 && m.sets==1) || (m.mode==13 && m.sets==3)) { ((unsigned char *)out)[0]^=1; }
        }return BLOCK_OK;
    }
    ++m.sets;assert(!out && !capacity);
    if (!strcmp(name,"event_msgs")) {
        assert(len==16);if (m.mode==12 && m.sets==3) { return BLOCK_IO; }memcpy(m.mask,input,16);
    } else {
        assert(!strcmp(name,"escan") && m.ups==1 && (m.mask[8]&32));const unsigned char *p=input;
        unsigned v=m.mode==1 ? 2 : 1;assert(g32(p)==v && len==(v==2 ? 80u : 72u));assert(p[4]==1 && !p[5] && p[6]==0x53 && p[7]==0x4f);
        const unsigned char *params=p+8;unsigned off=v==2 ? 4 : 0;
        for (unsigned i=0;i<36;++i) { assert(params[off+i]==0); }for (unsigned i=0;i<6;++i) { assert(params[off+36+i]==255); }
        assert(params[off+42]==2 && (v==2 ? g32(params+48) : params[43])==1);
        unsigned probes=v==2 ? 52 : 44;assert(g32(params+probes)==UINT32_MAX && g32(params+probes+4)==UINT32_MAX && g32(params+probes+8)==150 && g32(params+probes+12)==UINT32_MAX && !g32(params+probes+16));
        if (m.mode==9) { *status=0xfffffffeu;return BLOCK_UNSUPPORTED; }
        if (m.mode==16) { unsigned n=make_event(8,true);brcm_scan_event(&scan,event,n);n=make_event(0,false);brcm_scan_event(&scan,event,n); }
    }return BLOCK_OK;
}
static enum block_result radio(void *ctx,bool up,uint32_t *status)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }*status=0;
    if (up) { ++m.ups;assert(scan.interface_may_be_up && (m.mask[8]&32));if (m.mode==8) { return BLOCK_IO; } }
    else { ++m.downs;assert(scan.complete && scan.event_status==0);if (m.mode==11) { return BLOCK_IO; } }return BLOCK_OK;
}
static enum block_result poll(void *ctx)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }++m.polls;++m.ms;
    if (m.mode==2) { return BLOCK_OK; }
    unsigned n=make_event(m.polls==1 && m.mode!=14 ? 8 : m.mode==10 ? 4 : 0,m.polls==1 && m.mode!=14);
    if (m.mode==15 && m.polls==1) { p32(event+4+72+12+4,0xffffffffu); }
    brcm_scan_event(&scan,event,n);return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us) { (void)ctx;assert(us==10000);m.ms+=10;return touch(); }
static uint64_t now(void *ctx) { (void)ctx;return m.ms; }
static void seed(unsigned mode) { memset(&m,0,sizeof(m));m.mode=mode;m.mask[0]=8;memset(&scan,0,sizeof(scan)); }
int main(void)
{
    struct brcm_scan_io io={NULL,iovar,radio,poll,delay,now};struct brcm_prepare_state prep={.ready=true,.clm_complete=true,.country="IN",.ccode="IN",.country_revision=0};
    seed(0);assert(brcm_scan_run(&io,&prep,&scan)==BLOCK_OK && scan.ready && scan.phase==8 && scan.count==1 && scan.interface_down && scan.mask_restored && !scan.interface_may_be_up && m.mask[0]==8 && !m.mask[8]);
    assert(scan.ap[0].rssi==-49 && scan.ap[0].channel==6 && (scan.ap[0].security&(SCAN_SAE|SCAN_PSK))==(SCAN_SAE|SCAN_PSK) && scan.ap[0].pmf_required);
    unsigned calls=m.calls,total=calls;assert(brcm_scan_run(&io,&prep,&scan)==BLOCK_OK && m.calls==calls);
    for (unsigned mode=1;mode<=16;++mode) {
        seed(mode);enum block_result r=brcm_scan_run(&io,&prep,&scan);
        if (mode==1 || mode==14 || mode==15 || mode==16) { assert(r==BLOCK_OK && scan.ready); }else { assert(r!=BLOCK_OK && !scan.ready); }
        if (mode==2) { assert(r==BLOCK_TIMEOUT && !m.downs && scan.interface_may_be_up); }
        if (mode==3 || mode==4 || mode==5 || mode==6) { assert(!m.ups && !m.sets); }
        if (mode==14) { assert(!scan.count); }if (mode==15) { assert(scan.malformed==1); }
        calls=m.calls;if (scan.attempted) { assert(brcm_scan_run(&io,&prep,&scan)==r && m.calls==calls); }
    }
    for (unsigned fail=1;fail<=total;++fail) { seed(0);m.fail_at=fail;assert(brcm_scan_run(&io,&prep,&scan)==BLOCK_IO && m.calls==fail && !scan.ready); }
    seed(0);prep.ready=false;assert(brcm_scan_run(&io,&prep,&scan)==BLOCK_NOT_READY && !m.calls);prep.ready=true;
    unsigned n=make_bss();struct brcm_scan_ap ap;assert(brcm_scan_bss(bss,n,&ap)==BLOCK_OK);
    p16(bss+116,128);p32(bss+120,0);p32(bss+4,128);assert(brcm_scan_bss(bss,128,&ap)==BLOCK_OK);
    n=make_bss();bss[18]=33;assert(brcm_scan_bss(bss,n,&ap)==BLOCK_RANGE);
    n=make_bss();p16(bss+116,125);assert(brcm_scan_bss(bss,n,&ap)==BLOCK_RANGE);
    n=make_bss();bss[126+24]=64;assert(brcm_scan_bss(bss,n,&ap)==BLOCK_RANGE);
    n=make_bss();bss[126+14]=255;bss[126+15]=255;assert(brcm_scan_bss(bss,n,&ap)==BLOCK_RANGE);
    n=make_bss();p16(bss+72,0xe02a);bss[88]=0;assert(brcm_scan_bss(bss,n,&ap)==BLOCK_OK && ap.band==5 && ap.channel==36);
    n=make_bss();p32(bss,108);assert(brcm_scan_bss(bss,n,&ap)==BLOCK_UNSUPPORTED);
    seed(0);scan.armed=true;scan.sync_id=0x4f53;n=make_event(8,true);brcm_scan_event(&scan,event,n);assert(scan.count==1);
    unsigned char *bi=event+4+72+12;p16(bi+78,(uint16_t)-40);bi[18]=0;brcm_scan_event(&scan,event,n);
    assert(scan.count==1 && scan.duplicates==1 && scan.ap[0].rssi==-40 && scan.ap[0].ssid_length==7);
    for (unsigned i=1;i<33;++i) { n=make_event(8,true);event[4+72+12+13]=(unsigned char)i;brcm_scan_event(&scan,event,n); }
    assert(scan.count==32 && scan.truncated && scan.overflow==1);
    unsigned ignored=scan.ignored;n=make_event(8,true);p16(event+4+72+8,0x4321);brcm_scan_event(&scan,event,n);assert(scan.ignored==ignored+1);
    n=make_event(0,false);brcm_scan_event(&scan,event,n);assert(scan.complete);
    unsigned char original[1024];n=make_event(8,true);memcpy(original,event,n);uint32_t random=123;
    for (unsigned i=0;i<5000;++i) { memcpy(event,original,n);random=random*1664525u+1013904223u;event[random%n]^=(unsigned char)(random>>24);scan=(struct brcm_scan_state){.armed=true,.sync_id=0x4f53};brcm_scan_event(&scan,event,i%7==0 ? random%n : n);assert(scan.count<=32); }
    printf("PASS: passive escan v1/v2, live country/mask checks, UP/scan/DOWN/restore lifecycle, synchronous events, %u operation failures, timeouts and cached refusal\n",total);
    puts("PASS: bounded BCDC/event/BSS109/IE/RSN parsing, 126/128-byte IE offsets, WPA2+SAE/PMF, channel/RSSI, hidden/duplicate/overflow results and 5000 packet mutations");return 0;
}
