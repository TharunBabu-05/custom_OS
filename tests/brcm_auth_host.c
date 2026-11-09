#include <stdio.h>
#include <string.h>
#include "brcm_auth.h"
#include "host_assert.h"
static void p16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void p32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static void b16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void b32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(24-8*i)); } }
static uint32_t g32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static const struct brcm_auth_peer peer={.local={2,0,0,0,0,1},.peer={2,0,0,0,0,2},.ssid="public-test",.ssid_length=11,.channel=6,.band=2};
static unsigned char event[512],body[98];
static unsigned fixture(unsigned code,unsigned bytes)
{
    memset(event,0,sizeof(event));event[0]=0x20;unsigned char *p=event+4;
    b16(p+12,0x886c);b16(p+14,0x8001);p[20]=16;p[21]=24;b16(p+22,1);b16(p+24,2);b32(p+28,code);b32(p+44,bytes);memcpy(p+48,peer.peer,6);
    if (code==188) {
        unsigned char *d=p+72;b16(d+2,0x1006);p16(d+40,3);p16(d+42,1);p16(d+44,126);memcpy(d+46,body,98);
    } else if (code==187) { unsigned char *d=p+72;memcpy(d+2,peer.peer,6);p32(d+8,peer.ssid_length);memcpy(d+12,peer.ssid,peer.ssid_length); }
    else { p32(p+72,0x12345678); }
    return 76+bytes;
}
static struct { uint32_t values[4];unsigned char mask[BRCM_AUTH_MASK_MAX];unsigned calls,sets,fail_at,mode,mask_bytes;bool poisoned; } model;
static const char *const names[]={"auth","wpa_auth","wsec","mfp"};
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t bytes,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    (void)ctx;++model.calls;*actual=0;*status=0;
    if (model.poisoned) { return BLOCK_NOT_READY; }
    if (model.calls==model.fail_at) { *status=0xffffffe9u;if (model.mode==4) { model.poisoned=true; }return BLOCK_IO; }
    unsigned index=4;for (unsigned i=0;i<4;++i) { if (!strcmp(name,names[i])) { index=i; } }
    if (index<4) {
        if (set) { assert(bytes==4 && !output && !capacity);++model.sets;model.values[index]=g32(input);if (index==2) { model.values[3]=0; } }
        else { assert(!bytes && !input && capacity==4);p32(output,model.mode==3 && model.sets==5 && index==3 ? 0 : model.values[index]);*actual=4; }
    } else {
        assert(!strcmp(name,"event_msgs_ext"));const unsigned char *p=input;assert(p && p[0]==1);
        if (set) { assert(bytes==4+model.mask_bytes && p[1]==3 && p[2]==model.mask_bytes && !p[3] && !output);memcpy(model.mask,p+4,model.mask_bytes);++model.sets; }
        else {
            assert(bytes==259 && capacity==259 && !p[1] && p[2]==255 && p[3]==255);
            if (model.mode==1) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
            unsigned char *d=output;memset(d,0,259);d[0]=1;d[2]=(unsigned char)(model.mode==2 ? 16 : model.mask_bytes);d[3]=255;memcpy(d+4,model.mask,model.mask_bytes);*actual=4+model.mask_bytes;
            if (model.mode==5) { *actual=3; }
            if (model.mode==6) { d[0]=2; }
            if (model.mode==7) { --*actual; }
        }
    }
    return BLOCK_OK;
}
static void seed(void)
{ memset(&model,0,sizeof(model));model.mask_bytes=25;model.values[3]=1;model.mask[0]=0xa5;model.mask[8]=8;model.mask[23]=0x80;model.mask[24]=0x5a; }
int main(void)
{
    for (unsigned i=0;i<98;++i) { body[i]=(unsigned char)i; }body[0]=19;body[1]=0;
    unsigned char params[140];memset(params,0xa5,sizeof(params));size_t bytes=0;
    assert(brcm_auth_encode(&peer,0x12345678,1,body,98,params,140,&bytes)==BLOCK_OK && bytes==136 && params[136]==0xa5);
    assert(!g32(params) && g32(params+4)==4000 && params[8]==104 && params[10]==0xb0 && params[12]==6 && !params[13]);
    assert(!memcmp(params+14,peer.peer,6) && !memcmp(params+20,peer.peer,6) && !params[26] && !params[27] && g32(params+28)==0x12345678);
    assert(params[32]==3 && !params[33] && params[34]==1 && !params[35] && params[36]==126 && !params[37] && !memcmp(params+38,body,98));
    assert(brcm_auth_encode(&peer,0,1,body,98,params,140,&bytes)==BLOCK_ARGUMENT && !bytes);
    assert(brcm_auth_encode(&peer,1,1,body,98,params,135,&bytes)==BLOCK_RANGE);
    assert(brcm_auth_encode(&peer,1,1,body,97,params,140,&bytes)==BLOCK_UNSUPPORTED);
    assert(brcm_auth_encode(&peer,1,3,body,98,params,140,&bytes)==BLOCK_UNSUPPORTED);
    body[0]=20;assert(brcm_auth_encode(&peer,1,1,body,98,params,140,&bytes)==BLOCK_UNSUPPORTED);body[0]=19;
    unsigned char confirm[34]={1};assert(brcm_auth_encode(&peer,1,2,confirm,34,params,140,&bytes)==BLOCK_OK && bytes==72 && params[8]==40 && params[34]==2 && !params[36]);
    confirm[0]=0;assert(brcm_auth_encode(&peer,1,2,confirm,34,params,140,&bytes)==BLOCK_RANGE);
    confirm[0]=confirm[1]=255;assert(brcm_auth_encode(&peer,1,2,confirm,34,params,140,&bytes)==BLOCK_RANGE);
    struct brcm_auth_message message;unsigned n=fixture(188,144);
    assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_OK && message.kind==BRCM_AUTH_RX && message.sequence==1 && message.auth_status==126 && message.body_bytes==98 && !memcmp(message.body,body,98));
    for (unsigned i=0;i<n;++i) { assert(brcm_auth_event(&peer,0,event,i,&message)!=BLOCK_OK); }
    n=fixture(188,80);p16(event+76+42,2);p16(event+76+44,0);memcpy(event+76+46,confirm,34);
    assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_OK && message.sequence==2 && message.body_bytes==34);
    n=fixture(188,144);event[4+48]^=2;assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_NOT_READY);
    n=fixture(188,144);event[4+70]=1;assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_NOT_READY);
    n=fixture(188,144);b16(event+76+2,0x100b);assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_NOT_READY);
    n=fixture(188,144);p16(event+76+44,76);assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_UNSUPPORTED && message.auth_status==76);
    n=fixture(188,144);p16(event+76+40,0);assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_UNSUPPORTED);
    n=fixture(188,144);b32(event+4+44,145);assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_RANGE);
    n=fixture(187,44);memset(event+4+48,0,6);assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_OK && message.kind==BRCM_AUTH_REQUEST);
    event[76+12]^=1;assert(brcm_auth_event(&peer,0,event,n,&message)==BLOCK_NOT_READY);
    n=fixture(189,4);assert(brcm_auth_event(&peer,0x12345678,event,n,&message)==BLOCK_OK && message.packet_id==0x12345678);
    assert(brcm_auth_event(&peer,1,event,n,&message)==BLOCK_NOT_READY);
    b32(event+4+32,5);assert(brcm_auth_event(&peer,0x12345678,event,n,&message)==BLOCK_OK && message.status==5);
    n=fixture(190,4);assert(brcm_auth_event(&peer,0x12345678,event,n,&message)==BLOCK_OK && message.kind==BRCM_AUTH_OFFCHANNEL);
    n=fixture(69,0);assert(brcm_auth_event(&peer,1,event,n,&message)==BLOCK_NOT_READY);
    n=fixture(188,144);unsigned char original[512];memcpy(original,event,n);uint32_t random=0x42;
    for (unsigned i=0;i<5000;++i) { memcpy(event,original,n);random=random*1664525u+1013904223u;event[random%n]^=(unsigned char)(random>>24);(void)brcm_auth_event(&peer,1,event,i%9==0 ? random%n : n,&message);assert(message.body_bytes<=98); }
    puts("PASS: byte-exact mgmt_frame v0 commit/confirm encoding, mixed endianness/padding, bounded BCDC external-auth/RX/TX events, target/channel/cookie filters and 5000 mutations");
    const struct brcm_auth_probe_io io={NULL,iovar};struct brcm_auth_probe state={0};seed();
    assert(brcm_auth_probe_run(&io,false,&state)==BLOCK_NOT_READY && !model.calls && !state.attempted);
    assert(brcm_auth_probe_run(&io,true,&state)==BLOCK_OK && state.verified && state.restored && state.phase==6 && state.mask_bytes==25 && state.observed_mask==15);
    assert(model.values[0]==0 && model.values[1]==0 && model.values[2]==0 && model.values[3]==1 && model.mask[23]==0x80 && model.mask[24]==0x5a);
    unsigned total=model.calls;assert(total==25);assert(brcm_auth_probe_run(&io,true,&state)==BLOCK_OK && model.calls==total);
    for (unsigned fail=1;fail<=total;++fail) {
        seed();state=(struct brcm_auth_probe){0};model.fail_at=fail;
        assert(brcm_auth_probe_run(&io,true,&state)!=BLOCK_OK);
        if (fail<=5) { assert(!state.changed && !model.sets); }
        else if (fail<=15) { assert(state.restored && !model.values[0] && !model.values[1] && !model.values[2] && model.values[3]==1 && model.mask[23]==0x80); }
        else { assert(!state.restored && state.restore_error!=BLOCK_OK); }
        unsigned calls=model.calls;assert(brcm_auth_probe_run(&io,true,&state)==state.error && calls==model.calls);
    }
    for (unsigned mode=1;mode<=7;++mode) {
        seed();state=(struct brcm_auth_probe){0};model.mode=mode;
        if (mode==4) { model.fail_at=7; }
        assert(brcm_auth_probe_run(&io,true,&state)!=BLOCK_OK);
        if (mode<=2 || mode>=5) { assert(!state.changed && !model.sets); }
        if (mode==3) { assert(state.restored && !state.verified); }
        if (mode==4) { assert(!state.restored); }
    }
    const unsigned lengths[]={24,25,32,33,64,128,255};
    for (unsigned j=0;j<sizeof(lengths)/sizeof(lengths[0]);++j) {
        seed();state=(struct brcm_auth_probe){0};model.mask_bytes=lengths[j];
        unsigned char saved[BRCM_AUTH_MASK_MAX];for (unsigned k=0;k<model.mask_bytes;++k) { model.mask[k]=(unsigned char)(k*71+19); }memcpy(saved,model.mask,sizeof(saved));
        assert(brcm_auth_probe_run(&io,true,&state)==BLOCK_OK && state.mask_bytes==lengths[j] && state.mask_reply_bytes==4+lengths[j] && state.mask_header[2]==lengths[j]);
        assert(state.restored && !memcmp(model.mask,saved,sizeof(saved)));
    }
    puts("PASS: reversible SAE/AES/PMF, complete 24..255-byte mask preservation, reply-header diagnostics, 25 operation failures, unsupported/truncated masks and cached retry refusal");return 0;
}
