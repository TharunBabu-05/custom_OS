/* Upload/readback only. No reset-vector activation, CPU release or RF traffic. */
#include "brcm_firmware.h"
#include "sha256.h"
const struct brcm_fw_asset brcm_fw_assets[BRCM_FW_ASSETS]={
    {"FW43455.BIN","D608F866582519C0A28D86DB43040F4F1B98DD1D153E72E9752586546B4A36C3",609309},
    {"WIFINV.TXT","CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D",2074},
    {"WIFICLM.BIN","9823842CAE9FB9A5DD1E5FB31F595516EC7DEEE341354BEF30BB3026EEE29CC1",2676}};
static bool hash_matches(const unsigned char *digest,const char *expected)
{
    static const char hex[]="0123456789ABCDEF";
    for (unsigned i=0;i<32;++i) { if (hex[digest[i]>>4]!=expected[i*2] || hex[digest[i]&15]!=expected[i*2+1]) { return false; } }
    return true;
}
static bool equal(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }return true; }
enum block_result brcm_fw_check_files(const struct brcm_fw_io *io,struct brcm_fw_state *s)
{
    if (!io || !io->open || !io->file_read || !s) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }
    s->phase=FW_FILES;s->files_valid=false;enum block_result r;
    for (unsigned a=0;a<BRCM_FW_ASSETS;++a) {
        s->asset=a;uint32_t bytes;r=io->open(io->context,a,&bytes);if (r!=BLOCK_OK) { goto fail; }
        if (bytes!=brcm_fw_assets[a].bytes) { r=BLOCK_UNSUPPORTED;goto fail; }
        struct sha256 hash;sha256_init(&hash);
        for (uint32_t offset=0;offset<bytes;) {
            size_t n=bytes-offset;if (n>512) { n=512; }
            r=io->file_read(io->context,offset,s->buffer,n);if (r!=BLOCK_OK) { goto fail; }
            sha256_update(&hash,s->buffer,n);
            if (a==1) { for (size_t i=0;i<n;++i) { s->source_nvram[offset+i]=s->buffer[i]; } }
            offset+=(uint32_t)n;
        }
        sha256_final(&hash,s->digest[a]);
        if (!hash_matches(s->digest[a],brcm_fw_assets[a].sha256)) { r=BLOCK_IO;goto fail; }
    }
    s->files_valid=true;s->error=BLOCK_OK;return BLOCK_OK;
fail:
    s->error=r;return r;
}
static bool prefix(const unsigned char *p,size_t n,const char *word)
{ size_t i=0;while (word[i]) { if (i==n || p[i]!=(unsigned char)word[i]) { return false; }++i; }return i==n; }
enum block_result brcm_nvram_pack(const void *text,size_t length,const unsigned char mac[6],void *output,size_t capacity,uint32_t *packed)
{
    if (!text || !mac || !output || !packed || !length || length>4096 || capacity>4096 || capacity<32 || (mac[0]&1)) { return BLOCK_ARGUMENT; }
    unsigned nonzero=0;for (unsigned i=0;i<6;++i) { nonzero|=mac[i]; }if (!nonzero) { return BLOCK_ARGUMENT; }
    const unsigned char *p=text;unsigned char *out=output;size_t at=0,used=0;bool boardrev=false;
    while (at<length) {
        size_t start=at,end;
        while (at<length && p[at]!='\n') { if ((!p[at] || p[at]<32) && p[at]!='\r' && p[at]!='\t') { return BLOCK_UNSUPPORTED; }if (p[at]>126) { return BLOCK_UNSUPPORTED; }++at; }
        end=at;if (at<length) { ++at; }
        for (size_t i=start;i<end;++i) { if (p[i]=='#') { end=i;break; } }
        while (start<end && (p[start]==' ' || p[start]=='\t' || p[start]=='\r')) { ++start; }
        while (end>start && (p[end-1]==' ' || p[end-1]=='\t' || p[end-1]=='\r')) { --end; }
        if (start==end) { continue; }
        size_t split=start;while (split<end && p[split]!='=') {
            unsigned c=p[split];if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) { return BLOCK_UNSUPPORTED; }++split;
        }
        if (split==start || split==end || split+1==end) { return BLOCK_UNSUPPORTED; }
        if (prefix(p+start,split-start,"macaddr")) { continue; }
        if (prefix(p+start,split-start,"boardrev")) { boardrev=true; }
        if (end-start+1>capacity-used) { return BLOCK_RANGE; }
        while (start<end) { out[used++]=p[start++]; }out[used++]=0;
    }
    if (!boardrev || !used) { return BLOCK_UNSUPPORTED; }
    static const char key[]="macaddr=",hex[]="0123456789abcdef";
    if (26>capacity-used) { return BLOCK_RANGE; }
    for (unsigned i=0;i<8;++i) { out[used++]=(unsigned char)key[i]; }
    for (unsigned i=0;i<6;++i) { if (i) { out[used++]=':'; }out[used++]=(unsigned char)hex[mac[i]>>4];out[used++]=(unsigned char)hex[mac[i]&15]; }out[used++]=0;
    size_t aligned=(used+1+3)&~(size_t)3;
    if (aligned+4>capacity) { return BLOCK_RANGE; }
    while (used<aligned) { out[used++]=0; }
    uint32_t words=(uint32_t)aligned/4,token=(~words<<16)|(words&65535);
    for (unsigned i=0;i<4;++i) { out[used++]=(unsigned char)(token>>(i*8)); }*packed=(uint32_t)used;return BLOCK_OK;
}
bool brcm_fw_profile(uint32_t chip,const struct brcm_chip_info *c)
{
    if ((chip&0xf00fffff)!=0x10064345 || !c || !c->cores_valid || !c->ram_valid || !c->bank_restored || c->error!=BLOCK_OK || c->core_count!=5 || c->ram_base!=0x198000 || c->ram_bytes!=819200 || c->banks!=8) { return false; }
    static const uint16_t id[]={0x800,0x812,0x83e,0x83c,0x829};static const uint8_t rev[]={51,54,9,14,21};
    for (unsigned i=0;i<5;++i) {
        bool found=false;for (unsigned j=0;j<5;++j) { if (c->core[j].id==id[i] && c->core[j].revision==rev[i] && c->core[j].base==0x18000000+i*4096 && c->core[j].wrapper==0x18100000+i*4096) { found=true; } }
        if (!found) { return false; }
    }
    return true;
}
static enum block_result reg_read(const struct brcm_fw_io *io,struct brcm_fw_state *s,uint32_t at,uint32_t *value)
{ s->last_address=at;enum block_result r=io->read32(io->context,at,value);return r==BLOCK_OK && *value==UINT32_MAX ? BLOCK_IO : r; }
static enum block_result reg_write(const struct brcm_fw_io *io,struct brcm_fw_state *s,uint32_t at,uint32_t value)
{ s->last_address=at;return io->write32(io->context,at,value); }
static enum block_result passive(const struct brcm_fw_io *io,struct brcm_fw_state *s)
{
    uint32_t control,reset;enum block_result r=reg_read(io,s,0x18102408,&control);if (r!=BLOCK_OK) { return r; }
    r=reg_read(io,s,0x18102800,&reset);if (r!=BLOCK_OK) { return r; }
    if ((control&0x23)!=0x21 || (reset&1)) { return BLOCK_NOT_READY; }s->cpu_halted=true;
    r=reg_read(io,s,0x18101800,&reset);if (r!=BLOCK_OK) { return r; }
    s->attempted=true;s->phase=FW_PASSIVE;
    if (!(reset&1)) {
        r=reg_write(io,s,0x18101408,15);if (r!=BLOCK_OK) { return r; }
        r=reg_read(io,s,0x18101408,&control);if (r!=BLOCK_OK) { return r; }if ((control&15)!=15) { return BLOCK_IO; }
        r=reg_write(io,s,0x18101800,1);if (r!=BLOCK_OK) { return r; }
        r=io->delay_us(io->context,10);if (r!=BLOCK_OK) { return r; }
        bool ready=false;
        for (unsigned i=0;i<100;++i) {
            r=reg_read(io,s,0x18101800,&reset);if (r!=BLOCK_OK) { return r; }if (reset&1) { ready=true;break; }
            r=io->delay_us(io->context,10);if (r!=BLOCK_OK) { return r; }
        }
        if (!ready) { return BLOCK_TIMEOUT; }
    }
    r=reg_write(io,s,0x18101408,7);if (r!=BLOCK_OK) { return r; }
    r=reg_read(io,s,0x18101408,&control);if (r!=BLOCK_OK) { return r; }
    r=reg_read(io,s,0x18101800,&reset);if (r!=BLOCK_OK) { return r; }
    if ((control&7)!=7 || !(reset&1)) { return BLOCK_IO; }s->d11_disabled=true;return BLOCK_OK;
}
static enum block_result verified_write(const struct brcm_fw_io *io,struct brcm_fw_state *s,uint32_t at,unsigned char *data,size_t n)
{
    s->last_address=at;enum block_result r=io->ram(io->context,true,at,data,n);if (r!=BLOCK_OK) { return r; }
    r=io->ram(io->context,false,at,s->check,n);if (r!=BLOCK_OK) { return r; }
    return equal(data,s->check,n) ? BLOCK_OK : BLOCK_IO;
}
enum block_result brcm_fw_upload(const struct brcm_fw_io *io,uint32_t chip,const struct brcm_chip_info *cores,const unsigned char mac[6],struct brcm_fw_state *s)
{
    if (!io || !s || !mac || !io->open || !io->file_read || !io->read32 || !io->write32 || !io->ram || !io->delay_us) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }*s=(struct brcm_fw_state){.error=BLOCK_UNSUPPORTED};
    unsigned char zero[4]={0};
    if (!brcm_fw_profile(chip,cores)) { return s->error; }
    enum block_result r=brcm_fw_check_files(io,s);if (r!=BLOCK_OK) { return r; }
    s->phase=FW_LAYOUT;r=brcm_nvram_pack(s->source_nvram,brcm_fw_assets[1].bytes,mac,s->nvram,sizeof(s->nvram),&s->nvram_size);if (r!=BLOCK_OK) { goto fail; }
    s->nvram_address=0x260000-s->nvram_size;
    if (((brcm_fw_assets[0].bytes+3)&~3u)>s->nvram_address-0x198000) { r=BLOCK_RANGE;goto fail; }
    uint32_t bytes;r=io->open(io->context,0,&bytes);if (r!=BLOCK_OK) { goto fail; }if (bytes!=brcm_fw_assets[0].bytes) { r=BLOCK_IO;goto fail; }
    r=passive(io,s);if (r!=BLOCK_OK) { goto fail; }
    r=verified_write(io,s,0x25fffc,zero,4);if (r!=BLOCK_OK) { goto fail; }s->token_cleared=true;
    s->phase=FW_CODE;struct sha256 hash;sha256_init(&hash);
    for (uint32_t offset=0;offset<bytes;) {
        size_t n=bytes-offset;if (n>512) { n=512; }size_t padded=(n+3)&~(size_t)3;
        r=io->file_read(io->context,offset,s->buffer,n);if (r!=BLOCK_OK) { goto fail; }
        if (!offset) { s->reset_vector=(uint32_t)s->buffer[0]|(uint32_t)s->buffer[1]<<8|(uint32_t)s->buffer[2]<<16|(uint32_t)s->buffer[3]<<24; }
        sha256_update(&hash,s->buffer,n);
        for (size_t i=n;i<padded;++i) { s->buffer[i]=0; }
        r=verified_write(io,s,0x198000+offset,s->buffer,padded);if (r!=BLOCK_OK) { goto fail; }
        offset+=(uint32_t)n;s->uploaded=offset;s->verified=offset;
        if (io->progress && (!(offset%65536) || offset==bytes)) { io->progress(io->context,offset,bytes); }
    }
    unsigned char digest[32];sha256_final(&hash,digest);if (!hash_matches(digest,brcm_fw_assets[0].sha256)) { s->files_valid=false;r=BLOCK_IO;goto fail; }
    s->phase=FW_NVRAM;
    for (uint32_t offset=0;offset<s->nvram_size-4;) {
        size_t n=s->nvram_size-4-offset;if (n>512) { n=512; }
        r=verified_write(io,s,s->nvram_address+offset,s->nvram+offset,n);if (r!=BLOCK_OK) { goto fail; }offset+=(uint32_t)n;
    }
    /* Check persistent RAM after all payload writes. Per-chunk comparisons
     * alone cannot detect window aliasing or NVRAM overwriting the code. */
    sha256_init(&hash);s->verified=0;
    for (uint32_t offset=0;offset<bytes;) {
        size_t n=bytes-offset;if (n>512) { n=512; }
        s->last_address=0x198000+offset;
        r=io->ram(io->context,false,s->last_address,s->check,(n+3)&~(size_t)3);if (r!=BLOCK_OK) { goto fail; }
        sha256_update(&hash,s->check,n);offset+=(uint32_t)n;s->verified=offset;
    }
    sha256_final(&hash,digest);if (!hash_matches(digest,brcm_fw_assets[0].sha256)) { s->verified=0;r=BLOCK_IO;goto fail; }
    r=verified_write(io,s,0x25fffc,s->nvram+s->nvram_size-4,4);if (r!=BLOCK_OK) { goto fail; }
    uint32_t value;r=reg_read(io,s,0x18102408,&value);if (r!=BLOCK_OK) { goto fail; }if ((value&0x23)!=0x21) { s->cpu_halted=false;r=BLOCK_IO;goto fail; }
    r=reg_read(io,s,0x18102800,&value);if (r!=BLOCK_OK) { goto fail; }if (value&1) { r=BLOCK_IO;goto fail; }
    r=reg_read(io,s,0x18101800,&value);if (r!=BLOCK_OK) { goto fail; }if (!(value&1)) { s->d11_disabled=false;r=BLOCK_IO;goto fail; }
    s->complete=true;s->phase=FW_VERIFIED;s->error=BLOCK_OK;return BLOCK_OK;
fail:
    if (s->token_cleared) { uint32_t failed=s->last_address;(void)verified_write(io,s,0x25fffc,zero,4);s->last_address=failed; }
    s->error=r;return r;
}
