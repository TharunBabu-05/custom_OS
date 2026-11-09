#include <stdio.h>
#include <string.h>
#include "brcm_prepare.h"
#include "host_assert.h"
static unsigned char clm[2676],uploaded[2676];
static struct brcm_caps_state caps;
static struct brcm_prepare_state prep;
static struct { unsigned calls,fail_at,mode,sets,loaded,country_sets; } m;
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static void put32(void *p,uint32_t v) { unsigned char *b=p;for (unsigned i=0;i<4;++i) { b[i]=(unsigned char)(v>>(8*i)); } }
static uint32_t get32(const void *p) { const unsigned char *b=p;return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
static enum block_result open_file(void *ctx,unsigned asset,uint32_t *bytes)
{ (void)ctx;assert(asset==2);if (touch()!=BLOCK_OK || m.mode==22) { return BLOCK_IO; }*bytes=sizeof(clm)+(m.mode==10);return BLOCK_OK; }
static enum block_result read_file(void *ctx,uint32_t at,void *out,size_t n)
{
    (void)ctx;assert(at<sizeof(clm) && n<=sizeof(clm)-at && n<=512);
    if (touch()!=BLOCK_OK || m.mode==12) { return BLOCK_IO; }memcpy(out,clm+at,n);if (m.mode==11 && !at) { ((unsigned char *)out)[0]^=1; }return BLOCK_OK;
}
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t n,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    (void)ctx;if (touch()!=BLOCK_OK) { return BLOCK_IO; }*actual=0;*status=0;
    if (set) {
        ++m.sets;assert(!output && !capacity);
        if (!strcmp(name,"clmload")) {
            assert(n>=12 && get32((const unsigned char *)input+8)==0);unsigned amount=get32((const unsigned char *)input+4);
            assert(amount==n-12 && amount<=1400 && amount<=sizeof(clm)-m.loaded);
            unsigned flag=0x1000|(m.loaded ? 0u : 2u)|(m.loaded+amount==sizeof(clm) ? 4u : 0u);
            assert(get32(input)==(2u<<16|flag));
            if (m.mode==13 || (m.mode==14 && m.loaded)) { *status=0xffffffffu;return BLOCK_UNSUPPORTED; }
            memcpy(uploaded+m.loaded,(const unsigned char *)input+12,amount);m.loaded+=amount;
        } else {
            assert(!strcmp(name,"country") && n==12 && m.loaded==2676 && prep.clm_complete);
            const unsigned char *p=input;assert(!memcmp(p,"IN\0\0",4) && get32(p+4)==UINT32_MAX && !memcmp(p+8,"IN\0\0",4));
            if (m.mode==18) { *status=0xfffffffeu;return BLOCK_UNSUPPORTED; }++m.country_sets;
        }return BLOCK_OK;
    }
    assert(!input && !n);memset(output,0,capacity);
    if (!strcmp(name,"cap")) {
        assert(capacity==768);const char *text=m.mode==1 ? "not_sae sae2 sae_ext_extra" : "mbss sae extsae p2p 802.11h";
        memcpy(output,text,strlen(text)+1);*actual=capacity;
        if (m.mode==3) { memset(output,'x',capacity); }if (m.mode==4) { ((unsigned char *)output)[0]=27; }
    } else if (!strcmp(name,"cur_etheraddr")) {
        const unsigned char mac[]={0x2c,0xcf,0x67,0xf4,0x99,0x75};assert(capacity==6);memcpy(output,mac,6);*actual=6;
        if (m.mode==5) { ((unsigned char *)output)[0]|=1; }if (m.mode==6) { memset(output,0,6); }
    } else if (!strcmp(name,"mfp") || !strcmp(name,"sup_wpa")) {
        assert(capacity==4);*actual=m.mode==8 ? 3 : 4;
        if (m.mode==2) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
        if (m.mode==24) { *status=0xfffffffcu;return BLOCK_UNSUPPORTED; }
        if ((m.mode==7 && !strcmp(name,"mfp")) || (m.mode==9 && !strcmp(name,"sup_wpa"))) { return BLOCK_IO; }
    } else if (!strcmp(name,"clmload_status")) {
        assert(capacity==4 && m.loaded==2676);*actual=m.mode==16 ? 3 : 4;put32(output,m.mode==15 ? 5 : 0);
    } else if (!strcmp(name,"clmver")) {
        assert(capacity==256);*actual=capacity;memcpy(output,"mock regulatory data\nversion 1",30);
        if (m.mode==17) { memset(output,'x',capacity); }if (m.mode==23) { *status=0xffffffe9u;return BLOCK_UNSUPPORTED; }
    } else {
        assert(!strcmp(name,"country") && capacity==12 && m.country_sets==1);*actual=12;
        memcpy(output,m.mode==19 ? "US\0\0" : "IN\0\0",4);put32((unsigned char *)output+4,m.mode==21 ? UINT32_MAX : 0);
        memcpy((unsigned char *)output+8,m.mode==20 ? "X2\0\0" : "IN\0\0",4);
    }return BLOCK_OK;
}
static void seed(unsigned mode) { memset(&m,0,sizeof(m));m.mode=mode;memset(&caps,0,sizeof(caps));memset(&prep,0,sizeof(prep));memset(uploaded,0,sizeof(uploaded)); }
int main(void)
{
    FILE *file=fopen(".dist/wifi-firmware/WIFICLM.BIN","rb");assert(file && fread(clm,1,sizeof(clm),file)==sizeof(clm));assert(fgetc(file)==EOF);fclose(file);
    struct brcm_prepare_io io={NULL,iovar,open_file,read_file};seed(0);
    assert(brcm_caps_read(&io,&caps)==BLOCK_OK && caps.ready && caps.sae && caps.external_sae && caps.mfp_supported && caps.supplicant_supported && !caps.mfp_value && !caps.supplicant_value);
    unsigned cap_calls=m.calls,calls=m.calls;assert(brcm_caps_read(&io,&caps)==BLOCK_OK && m.calls==calls);
    seed(1);assert(brcm_caps_read(&io,&caps)==BLOCK_OK && !caps.sae && !caps.external_sae);
    seed(2);assert(brcm_caps_read(&io,&caps)==BLOCK_OK && caps.mfp_known && !caps.mfp_supported && caps.supplicant_known && !caps.supplicant_supported);
    seed(24);assert(brcm_caps_read(&io,&caps)==BLOCK_OK && !caps.mfp_known && !caps.supplicant_known && caps.mfp_status==0xfffffffcu && caps.supplicant_status==0xfffffffcu);
    for (unsigned mode=3;mode<=9;++mode) { seed(mode);assert(brcm_caps_read(&io,&caps)==BLOCK_IO && !caps.ready); }
    seed(0);assert(brcm_wifi_prepare(&io,"IN",&prep)==BLOCK_OK && prep.ready && prep.files_verified && prep.clm_complete && prep.clm_status_valid && !prep.clm_status && prep.uploaded==2676 && prep.chunks==2 && !strcmp(prep.country,"IN") && !strcmp(prep.ccode,"IN"));
    assert(!memcmp(clm,uploaded,sizeof(clm)));unsigned prep_calls=m.calls;calls=m.calls;
    assert(brcm_wifi_prepare(&io,"IN",&prep)==BLOCK_OK && m.calls==calls);
    for (unsigned mode=10;mode<=23;++mode) {
        seed(mode);assert(brcm_wifi_prepare(&io,"IN",&prep)!=BLOCK_OK && !prep.ready);
        if (mode==10 || mode==11 || mode==12 || mode==22) { assert(!m.sets && !prep.attempted); }
        if (mode>=13 && mode<=17) { assert(!m.country_sets); }
        calls=m.calls;if (prep.attempted) { enum block_result old=prep.error;assert(brcm_wifi_prepare(&io,"IN",&prep)==old && m.calls==calls); }
    }
    for (unsigned fail=1;fail<=cap_calls;++fail) { seed(0);m.fail_at=fail;assert(brcm_caps_read(&io,&caps)==BLOCK_IO && m.calls==fail && !caps.ready); }
    for (unsigned fail=1;fail<=prep_calls;++fail) { seed(0);m.fail_at=fail;assert(brcm_wifi_prepare(&io,"IN",&prep)==BLOCK_IO && m.calls==fail && !prep.ready); }
    const char *bad[]={"","I","IND","in","00","XX","I ","IN ","I\n"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { seed(0);assert(brcm_wifi_prepare(&io,bad[i],&prep)==BLOCK_ARGUMENT && !m.calls); }
    assert(brcm_country_valid("IN") && brcm_country_valid("US") && !brcm_country_valid(NULL));
    printf("PASS: exact SAE tokens, measured MAC, PMF/supplicant query semantics; %u capability and %u regulatory operation failures\n",cap_calls,prep_calls);
    puts("PASS: real pinned CLM hash, byte-exact two-chunk flags/type/length/CRC, load-status/version checks, IN country set/readback, no silent fallback and one-shot failures");return 0;
}
