/* Capability and regulatory setup only. No interface-up, scan, association,
 * credentials or RF transmit command. Definitions: brcmfmac feature.c,
 * common.c and fwil_types.h; see docs/wifi-prepare.md. */
#include "brcm_prepare.h"
#include "brcm_firmware.h"
#include "sha256.h"
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool text_copy(char *out,size_t capacity,const unsigned char *in,size_t n,bool caps)
{
    if (n>capacity) { return false; }
    for (size_t i=0;i<n;++i) {
        unsigned ch=in[i];if (!ch) { out[i]=0;return i!=0; }
        if (ch>126 || (ch<32 && (caps || (ch!='\n' && ch!='\r' && ch!='\t')))) { return false; }
        out[i]=(char)(ch<32 ? ' ' : ch);
    }return false;
}
static bool token(const char *p,const char *name)
{
    while (*p) {
        while (*p==' ') { ++p; }const char *start=p;while (*p && *p!=' ') { ++p; }
        size_t i=0;while (name[i] && start+i<p && name[i]==start[i]) { ++i; }
        if (!name[i] && start+i==p) { return true; }
    }return false;
}
bool brcm_country_valid(const char *p)
{ return p && p[0]>='A' && p[0]<='Z' && p[1]>='A' && p[1]<='Z' && !p[2] && !(p[0]=='X' && p[1]=='X'); }
#define TRY(call) do { r=(call);if (r!=BLOCK_OK) { goto fail; } } while (0)
#define REQUIRE(test,err) do { if (!(test)) { r=(err);goto fail; } } while (0)
enum block_result brcm_caps_read(const struct brcm_prepare_io *io,struct brcm_caps_state *s)
{
    if (!io || !io->iovar || !s) { return BLOCK_ARGUMENT; }if (s->attempted) { return s->error; }
    *s=(struct brcm_caps_state){.attempted=true,.phase=1,.error=BLOCK_NOT_READY};
    enum block_result r;size_t n;unsigned char value[768];
    TRY(io->iovar(io->context,false,"cap",NULL,0,value,sizeof(value),&n,&s->firmware_status));
    REQUIRE(text_copy(s->caps,sizeof(s->caps),value,n,true),BLOCK_IO);
    s->sae=token(s->caps,"sae");s->external_sae=token(s->caps,"sae_ext") || token(s->caps,"extsae");
    s->phase=2;TRY(io->iovar(io->context,false,"cur_etheraddr",NULL,0,value,6,&n,&s->firmware_status));
    REQUIRE(n==6 && !(value[0]&1),BLOCK_IO);unsigned nonzero=0;
    for (unsigned i=0;i<6;++i) { s->mac[i]=value[i];nonzero|=value[i]; }REQUIRE(nonzero,BLOCK_IO);
    s->phase=3;r=io->iovar(io->context,false,"mfp",NULL,0,value,4,&n,&s->firmware_status);
    s->mfp_status=s->firmware_status;
    if (r==BLOCK_OK) { REQUIRE(n==4,BLOCK_IO);s->mfp_known=true;s->mfp_supported=true;s->mfp_value=le32(value); }
    else if (r!=BLOCK_UNSUPPORTED || !s->firmware_status) { goto fail; }
    else { s->mfp_known=s->firmware_status==0xffffffe9u; }
    s->phase=4;r=io->iovar(io->context,false,"sup_wpa",NULL,0,value,4,&n,&s->firmware_status);
    s->supplicant_status=s->firmware_status;
    if (r==BLOCK_OK) { REQUIRE(n==4,BLOCK_IO);s->supplicant_known=true;s->supplicant_supported=true;s->supplicant_value=le32(value); }
    else if (r!=BLOCK_UNSUPPORTED || !s->firmware_status) { goto fail; }
    else { s->supplicant_known=s->firmware_status==0xffffffe9u; }
    s->phase=5;s->ready=true;s->error=BLOCK_OK;return BLOCK_OK;
fail:s->error=r;return r;
}
static bool hash_matches(const unsigned char *hash,const char *expected)
{
    static const char hex[]="0123456789ABCDEF";
    for (unsigned i=0;i<32;++i) { if (hex[hash[i]>>4]!=expected[i*2] || hex[hash[i]&15]!=expected[i*2+1]) { return false; } }return true;
}
enum block_result brcm_wifi_prepare(const struct brcm_prepare_io *io,const char *country,struct brcm_prepare_state *s)
{
    if (!io || !io->iovar || !io->open || !io->read || !s || !brcm_country_valid(country)) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }
    *s=(struct brcm_prepare_state){.phase=1,.error=BLOCK_NOT_READY};s->requested[0]=country[0];s->requested[1]=country[1];
    enum block_result r;uint32_t bytes;size_t n;
    TRY(io->open(io->context,2,&bytes));REQUIRE(bytes==sizeof(s->blob),BLOCK_IO);
    for (uint32_t at=0;at<bytes;) {
        size_t amount=bytes-at;if (amount>512) { amount=512; }
        TRY(io->read(io->context,at,s->blob+at,amount));at+=(uint32_t)amount;
    }
    unsigned char hash[32];struct sha256 sha;sha256_init(&sha);sha256_update(&sha,s->blob,sizeof(s->blob));sha256_final(&sha,hash);
    REQUIRE(hash_matches(hash,brcm_fw_assets[2].sha256),BLOCK_IO);s->files_verified=true;s->attempted=true;s->phase=2;
    while (s->uploaded<bytes) {
        uint32_t amount=bytes-s->uploaded;if (amount>1400) { amount=1400; }
        uint32_t flags=0x1000|(s->uploaded ? 0u : 2u)|(s->uploaded+amount==bytes ? 4u : 0u);
        put32(s->chunk,2u<<16|flags);put32(s->chunk+4,amount);put32(s->chunk+8,0);
        for (uint32_t i=0;i<amount;++i) { s->chunk[12+i]=s->blob[s->uploaded+i]; }
        TRY(io->iovar(io->context,true,"clmload",s->chunk,12+amount,NULL,0,&n,&s->firmware_status));
        s->uploaded+=amount;++s->chunks;
    }
    unsigned char value[256]={0};s->phase=3;
    TRY(io->iovar(io->context,false,"clmload_status",NULL,0,value,4,&n,&s->firmware_status));
    REQUIRE(n==4,BLOCK_IO);s->clm_status=le32(value);s->clm_status_valid=true;REQUIRE(!s->clm_status,BLOCK_IO);
    TRY(io->iovar(io->context,false,"clmver",NULL,0,value,sizeof(value),&n,&s->firmware_status));
    REQUIRE(text_copy(s->clm_version,sizeof(s->clm_version),value,n,false),BLOCK_IO);s->clm_complete=true;
    s->phase=4;for (unsigned i=0;i<12;++i) { value[i]=0; }
    value[0]=(unsigned char)country[0];value[1]=(unsigned char)country[1];put32(value+4,UINT32_MAX);
    value[8]=(unsigned char)country[0];value[9]=(unsigned char)country[1];
    TRY(io->iovar(io->context,true,"country",value,12,NULL,0,&n,&s->firmware_status));s->country_written=true;
    s->phase=5;TRY(io->iovar(io->context,false,"country",NULL,0,value,12,&n,&s->firmware_status));
    for (unsigned i=0;i<12 && i<n;++i) { s->country_reply[i]=value[i]; }
    REQUIRE(n==12 && value[0]==country[0] && value[1]==country[1] && !value[2] && !value[3],BLOCK_IO);
    REQUIRE(value[8]==country[0] && value[9]==country[1] && !value[10] && !value[11],BLOCK_IO);
    s->country[0]=(char)value[0];s->country[1]=(char)value[1];s->ccode[0]=(char)value[8];s->ccode[1]=(char)value[9];s->country_revision=le32(value+4);
    REQUIRE(s->country_revision!=UINT32_MAX,BLOCK_IO);
    s->ready=true;s->phase=6;s->error=BLOCK_OK;return BLOCK_OK;
fail:s->error=r;return r;
}
