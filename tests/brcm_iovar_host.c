#include <stdio.h>
#include <string.h>
#include "brcm_control.h"
#include "host_assert.h"
static struct brcm_control_state state;
static struct { unsigned calls,mode,position,bytes,tx,sequence;bool pending,echo_set;unsigned char response[2048]; } m;
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static enum block_result read32(void *ctx,uint32_t at,uint32_t *v)
{ (void)ctx;++m.calls;if (m.mode==3) { return BLOCK_IO; }assert(at==0x18004020 || at==0x1800404c);*v=at==0x18004020 ? (m.pending ? 0x40 : 0) : 0x40002;return BLOCK_OK; }
static enum block_result write32(void *ctx,uint32_t at,uint32_t v)
{ (void)ctx;++m.calls;assert(at==0x18004020 && v==0x40);return BLOCK_OK; }
static enum block_result delay(void *ctx,unsigned us) { (void)ctx;++m.calls;assert(us==10000);return BLOCK_OK; }
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    (void)ctx;++m.calls;unsigned char *p=buffer;
    if (write) {
        if (m.mode==2) { return BLOCK_IO; }
        assert(n<=2048 && !(n&3));struct brcm_sdpcm_header h;assert(brcm_sdpcm_header(p,n,&h)==BLOCK_OK && h.length+3u>=n && h.sequence==state.tx_seq);
        uint32_t command=get32(p+12),payload=get32(p+16),flags=get32(p+20);bool set=command==263;
        assert((command==262 || command==263) && !!(flags&2)==set && payload==h.length-28u && !get32(p+24));
        unsigned namelen=(unsigned)strlen((char *)p+28)+1;assert(namelen<=payload);
        if (!strcmp((char *)p+28,"cap")) { assert(!set && payload==772); }
        else if (!strcmp((char *)p+28,"cur_etheraddr")) { assert(!set && payload==20); }
        else if (!strcmp((char *)p+28,"mfp")) { assert(payload==8); }
        else if (!strcmp((char *)p+28,"auth") || !strcmp((char *)p+28,"wpa_auth") || !strcmp((char *)p+28,"wsec")) { assert(payload==namelen+4); }
        else if (!strcmp((char *)p+28,"event_msgs_ext")) {
            assert(payload==(set ? 44u : 274u) && p[28+namelen]==1);
            if (!set) { assert(p[29+namelen]==0 && p[30+namelen]==255 && p[31+namelen]==255); }
        }
        else if (!strcmp((char *)p+28,"mgmt_frame")) { assert(set && payload==147 && p[28+namelen+10]==0xb0 && get32(p+28+namelen+28)==0x12345678); }
        else { assert(!strcmp((char *)p+28,"clmload") && set && payload==1420 && n==1448);for (unsigned i=0;i<1412;++i) { assert(p[28+namelen+i]==(unsigned char)(i*19)); } }
        unsigned out=set ? 0 : payload,len=28+out;memset(m.response,0,sizeof(m.response));
        put32(m.response,((uint32_t)(uint16_t)~len<<16)|len);m.response[4]=(unsigned char)m.sequence++;m.response[7]=12;m.response[9]=(uint8_t)(state.tx_seq+4);
        put32(m.response+12,command);put32(m.response+16,m.mode==6 && set ? payload : m.mode==4 ? out+1 : out);
        put32(m.response+20,(flags&0xffff0000u)|(m.echo_set && set ? 2u : 0u)|(m.mode==1 ? 1u : m.mode==5 ? 2u : 0u));
        put32(m.response+24,m.mode==1 ? 0xffffffe9u : 0);
        for (unsigned i=0;i<out;++i) { m.response[28+i]=(unsigned char)(i*7); }
        m.bytes=(len+3)&~3u;m.position=0;m.pending=true;++m.tx;
    } else {
        assert(n<=512 && !(n&3));memset(p,0,n);
        if (!m.pending) { assert(n==64);return BLOCK_OK; }
        unsigned remain=m.bytes-m.position;assert((!m.position && n==64) || (m.position && n==(remain>512 ? 512 : remain)));
        unsigned copy=n<remain ? (unsigned)n : remain;memcpy(p,m.response+m.position,copy);m.position+=copy;
        if (m.position==m.bytes) { m.pending=false; }
    }return BLOCK_OK;
}
static void seed(void)
{ memset(&m,0,sizeof(m));state=(struct brcm_control_state){.ready=true,.f2_ready=true,.device_ready=true,.error=BLOCK_OK,.phase=CONTROL_READY,.tx_seq=2,.tx_max=22,.request_id=2}; }
int main(void)
{
    struct brcm_control_io io={.read32=read32,.write32=write32,.packet=packet,.delay_us=delay};
    unsigned char output[770],input[1412];size_t n;uint32_t status;
    seed();memset(output,0xa5,sizeof(output));
    assert(brcm_control_iovar(&io,&state,false,"cap",NULL,0,output,768,&n,&status)==BLOCK_OK && n==768 && !status && state.ready);
    for (unsigned i=0;i<768;++i) { assert(output[i]==(unsigned char)(i*7)); }assert(output[768]==0xa5 && output[769]==0xa5);
    assert(brcm_control_iovar(&io,&state,false,"cur_etheraddr",NULL,0,output,6,&n,&status)==BLOCK_OK && n==6);
    for (unsigned i=0;i<1412;++i) { input[i]=(unsigned char)(i*19); }
    assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK && !n);
    m.echo_set=true;assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK);
    m.mode=6;assert(brcm_control_iovar(&io,&state,true,"clmload",input,sizeof(input),NULL,0,&n,&status)==BLOCK_OK && !n);
    seed();m.mode=1;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_UNSUPPORTED && status==0xffffffe9u && state.ready);
    m.mode=0;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_OK && n==4);
    m.mode=1;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_UNSUPPORTED && state.ready);
    m.mode=3;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_IO && !state.ready);
    for (unsigned mode=2;mode<=5;++mode) {
        seed();m.mode=mode;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)!=BLOCK_OK && !state.ready && !state.waiting);
        unsigned calls=m.calls;assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_NOT_READY && m.calls==calls);
    }
    seed();state.tx_seq=255;state.tx_max=3;state.request_id=65535;m.sequence=255;state.rx_seq=255;
    assert(brcm_control_iovar(&io,&state,false,"mfp",NULL,0,output,4,&n,&status)==BLOCK_OK && state.tx_seq==0 && state.request_id==0 && state.rx_seq==0);
    seed();assert(brcm_control_iovar(&io,&state,true,"ssid",NULL,0,NULL,0,&n,&status)==BLOCK_UNSUPPORTED && !m.calls);
    assert(brcm_control_iovar(&io,&state,false,"cap",input,769,output,768,&n,&status)==BLOCK_ARGUMENT && !m.calls);
    unsigned char ext[259]={1,0,255,255},word[4]={3},auth[136]={0};
    assert(brcm_control_iovar(&io,&state,false,"event_msgs_ext",ext,259,output,259,&n,&status)==BLOCK_OK && n==259);
    ext[1]=3;ext[2]=25;ext[3]=0;
    assert(brcm_control_iovar(&io,&state,true,"event_msgs_ext",ext,29,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,true,"auth",word,4,NULL,0,&n,&status)==BLOCK_OK);
    assert(brcm_control_iovar(&io,&state,false,"wpa_auth",NULL,0,output,4,&n,&status)==BLOCK_OK && n==4);
    auth[10]=0xb0;put32(auth+28,0x12345678);
    assert(brcm_control_iovar(&io,&state,true,"mgmt_frame",auth,136,NULL,0,&n,&status)==BLOCK_OK);
    puts("PASS: extended-mask GET input header, policy IOVARs and management-frame SET transported through actual CDC/SDPCM framing");
    puts("PASS: restricted IOVAR GET/SET framing, 1448-byte CLM TX, 800-byte multi-part RX, padded results/copy bounds, SET reply variants and ID/sequence wrap");
    puts("PASS: rejected firmware request preserves transport, later I/O failure poisons it; no access after failure and no arbitrary SET interface");return 0;
}
