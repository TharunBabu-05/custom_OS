#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brcm_firmware.h"
#include "sha256.h"
#include "host_assert.h"
static unsigned char *assets[3];
static struct brcm_fw_state state;
struct mock {
    unsigned asset,opens[3],reads,writes,ram_writes,delays,mode;
    uint32_t d11_control,d11_reset;
    bool offline;
    unsigned char ram[819200];
};
static struct mock model;
static enum block_result open_file(void *context,unsigned asset,uint32_t *bytes)
{ struct mock *m=context;assert(asset<3);m->asset=asset;++m->opens[asset];*bytes=brcm_fw_assets[asset].bytes+(m->mode==2 && asset==0);return m->mode==15 ? BLOCK_IO : BLOCK_OK; }
static enum block_result read_file(void *context,uint32_t offset,void *data,size_t n)
{
    struct mock *m=context;++m->reads;assert(offset<=brcm_fw_assets[m->asset].bytes && n<=brcm_fw_assets[m->asset].bytes-offset);
    if (m->mode==4 && m->asset==0 && m->opens[0]>1 && offset>=65536) { return BLOCK_IO; }
    memcpy(data,assets[m->asset]+offset,n);
    if ((m->mode==1 && m->asset==0 && !offset) || (m->mode==13 && m->asset==2 && !offset) || (m->mode==14 && m->asset==0 && m->opens[0]>1 && !offset)) { ((unsigned char *)data)[0]^=1; }
    return BLOCK_OK;
}
static enum block_result read_reg(void *context,uint32_t at,uint32_t *value)
{
    struct mock *m=context;
    switch (at) {
    case 0x18102408:*value=m->mode==5 || (m->mode==12 && m->ram_writes>100) ? 1 : 0x21;break;
    case 0x18102800:*value=0;break;
    case 0x18101408:*value=m->d11_control;break;
    case 0x18101800:*value=m->d11_reset;break;
    default:assert(0);
    }
    return BLOCK_OK;
}
static enum block_result write_reg(void *context,uint32_t at,uint32_t value)
{
    struct mock *m=context;++m->writes;
    if (at==0x18101408) { assert(value==15 || value==7);m->d11_control=value; }
    else { assert(at==0x18101800 && value==1);if (m->mode!=7) { m->d11_reset=1; } }
    return BLOCK_OK;
}
static enum block_result ram(void *context,bool write,uint32_t at,void *data,size_t n)
{
    struct mock *m=context;assert(at>=0x198000 && at<0x260000 && n && n<=512 && !(at&3) && !(n&3) && n<=0x260000-at);
    assert(m->d11_reset==1 && m->d11_control==7);
    if (m->offline) { return BLOCK_NOT_READY; }
    if (write) {
        if (m->mode==8 && at>=0x1b8000 && at<0x250000) { m->offline=true;return BLOCK_TIMEOUT; }
        if (m->mode==11 && at==0x25fffc && ((const unsigned char *)data)[0]) { return BLOCK_IO; }
        ++m->ram_writes;memcpy(m->ram+(m->mode==17 ? (at-0x198000)&0x7fff : at-0x198000),data,n);
    } else {
        memcpy(data,m->ram+(m->mode==17 ? (at-0x198000)&0x7fff : at-0x198000),n);
        if ((m->mode==9 && at==0x198000) || (m->mode==10 && at==state.nvram_address)) { ((unsigned char *)data)[0]^=1; }
    }
    return BLOCK_OK;
}
static enum block_result delay(void *context,unsigned us)
{ struct mock *m=context;assert(us==10);++m->delays;return m->mode==16 ? BLOCK_TIMEOUT : BLOCK_OK; }
static struct brcm_chip_info profile(void)
{
    struct brcm_chip_info c={.error=BLOCK_OK,.cores_valid=true,.ram_valid=true,.bank_restored=true,.core_count=5,.ram_base=0x198000,.ram_bytes=819200,.banks=8};
    const uint16_t id[]={0x800,0x812,0x83e,0x83c,0x829};const uint8_t rev[]={51,54,9,14,21};
    for (unsigned i=0;i<5;++i) { c.core[i]=(struct brcm_core){id[i],rev[i],0x18000000+i*4096,0x18100000+i*4096}; }
    return c;
}
static void sha_tests(void)
{
    const char *input[]={"","abc","abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[]={"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (unsigned i=0;i<3;++i) {
        struct sha256 s;unsigned char digest[32];char text[65];sha256_init(&s);
        for (size_t j=0;j<strlen(input[i]);++j) { sha256_update(&s,input[i]+j,1); }sha256_final(&s,digest);
        for (unsigned j=0;j<32;++j) { sprintf(text+j*2,"%02x",digest[j]); }assert(!strcmp(text,expected[i]));
    }
    struct sha256 s;unsigned char digest[32];char text[65];sha256_init(&s);
    char block[1000];memset(block,'a',sizeof(block));for (unsigned i=0;i<1000;++i) { sha256_update(&s,block,sizeof(block)); }sha256_final(&s,digest);
    for (unsigned j=0;j<32;++j) { sprintf(text+j*2,"%02x",digest[j]); }
    assert(!strcmp(text,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
static void nvram_tests(const unsigned char mac[6])
{
    unsigned char out[4096];uint32_t n;
    const char text[]="# test\r\nboardrev=0x1304\r\nx=one # note\nmacaddr=00:00:00:00:00:00\nx=two\n";
    assert(brcm_nvram_pack(text,strlen(text),mac,out,sizeof(out),&n)==BLOCK_OK && !(n&3));
    bool found=false;for (unsigned i=0;i+26<=n-4;++i) { if (!memcmp(out+i,"macaddr=2c:cf:67:f4:99:75",26)) { found=true;break; } }assert(found);
    uint32_t token;memcpy(&token,out+n-4,4);assert((token&65535)==(n-4)/4 && (token>>16)==((~((n-4)/4))&65535));
    assert(brcm_nvram_pack("x=1\n",4,mac,out,sizeof(out),&n)==BLOCK_UNSUPPORTED);
    assert(brcm_nvram_pack("board rev=1\n",12,mac,out,sizeof(out),&n)==BLOCK_UNSUPPORTED);
    assert(brcm_nvram_pack(text,strlen(text),mac,out,32,&n)==BLOCK_RANGE);
    unsigned char bad[6]={1,2,3,4,5,6};assert(brcm_nvram_pack(text,strlen(text),bad,out,sizeof(out),&n)==BLOCK_ARGUMENT);
    unsigned char nul[]={ 'b','o','a','r','d','r','e','v','=','1',0,'\n' };assert(brcm_nvram_pack(nul,sizeof(nul),mac,out,sizeof(out),&n)==BLOCK_UNSUPPORTED);
}
int main(void)
{
    sha_tests();const unsigned char mac[6]={0x2c,0xcf,0x67,0xf4,0x99,0x75};nvram_tests(mac);
    for (unsigned i=0;i<3;++i) {
        char path[128];snprintf(path,sizeof(path),".dist/wifi-firmware/%s",brcm_fw_assets[i].name);FILE *f=fopen(path,"rb");assert(f);
        assets[i]=malloc(brcm_fw_assets[i].bytes);assert(assets[i] && fread(assets[i],1,brcm_fw_assets[i].bytes,f)==brcm_fw_assets[i].bytes);assert(fgetc(f)==EOF);fclose(f);
    }
    struct brcm_fw_io io={&model,open_file,read_file,read_reg,write_reg,ram,delay,NULL};struct brcm_chip_info c=profile();
    for (unsigned mode=0;mode<=17;++mode) {
        memset(&model,0,sizeof(model));model.mode=mode;state=(struct brcm_fw_state){0};
        if (mode==6) { model.d11_reset=1; }
        unsigned char selected_mac[6];memcpy(selected_mac,mac,6);if (mode==3) { selected_mac[0]=1; }
        enum block_result r=brcm_fw_upload(&io,0x15264345,&c,selected_mac,&state);
        if (!mode || mode==6) {
            assert(r==BLOCK_OK && state.complete && state.verified==609309 && state.cpu_halted && state.d11_disabled);
            assert(!memcmp(model.ram,assets[0],609309));assert(model.ram[609309]==0 && model.ram[609310]==0 && model.ram[609311]==0);
            assert(!memcmp(model.ram+state.nvram_address-0x198000,state.nvram,state.nvram_size));
        } else {
            assert(r!=BLOCK_OK && !state.complete);
            if (mode==1 || mode==2 || mode==3 || mode==5 || mode==13 || mode==15) { assert(!model.writes && !model.ram_writes && !state.attempted); }
            if (mode==7 || mode==16) { assert(state.attempted && !model.ram_writes && model.delays<=101); }
            if (mode==4 || mode==9 || mode==10 || mode==11 || mode==12 || mode==14) { assert(!memcmp(model.ram+819200-4,"\0\0\0\0",4)); }
            if (mode==8) { assert(r==BLOCK_TIMEOUT && state.attempted && model.offline); }
            if (mode==17) { assert(r==BLOCK_IO && state.phase==FW_NVRAM && !state.verified && !memcmp(model.ram+0x7ffc,"\0\0\0\0",4)); }
        }
        if (state.attempted) {
            unsigned writes=model.writes,reads=model.reads;
            assert(brcm_fw_upload(&io,0x15264345,&c,mac,&state)==BLOCK_NOT_READY && model.writes==writes && model.reads==reads);
        }
    }
    memset(&model,0,sizeof(model));state=(struct brcm_fw_state){0};c.core[2].revision++;
    assert(brcm_fw_upload(&io,0x15264345,&c,mac,&state)==BLOCK_UNSUPPORTED && !model.reads && !model.writes);
    for (unsigned i=0;i<3;++i) { free(assets[i]); }
    puts("PASS: SHA256 known vectors/million-a, pinned real assets, NVRAM comments/packing/MAC/token, exact observed profile, held CPU/D11, full padded upload/readback, no activation, file/hash/CRC-style/mismatch/timeout/cleanup paths and one-shot refusal");
    return 0;
}
