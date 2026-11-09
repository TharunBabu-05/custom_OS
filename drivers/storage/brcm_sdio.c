#include "brcm_sdio.h"
static enum block_result window(struct sdio_card *c,uint32_t address)
{
    uint32_t base=address&0xffff8000u;
    for (unsigned i=0;i<3;++i) {
        enum block_result r=sdio_write_byte(c,1,0x1000a+i,(uint8_t)(base>>(8+i*8)));
        if (r!=BLOCK_OK) { return r; }
    }
    return BLOCK_OK;
}
static enum block_result read32(struct sdio_card *c,uint32_t address,uint32_t *value)
{
    if (!value || (address&3) || address<0x18000000 || address>0x181ffffc) { return BLOCK_ARGUMENT; }
    enum block_result r=window(c,address); if (r!=BLOCK_OK) { return r; }
    unsigned char data[4]; r=sdio_read_bytes(c,1,(address&0x7fff)|0x8000,data,4);
    if (r==BLOCK_OK) { *value=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24; }
    return r;
}
static bool identified(const struct sdio_card *c,const struct brcm_sdio_state *s)
{
    return c && s && c->selected && c->vendor==0x02d0 && c->device==0x4345 &&
        s->chip_read && s->function1_ready && s->function1_block_size==BRCM_F1_BLOCK_SIZE && s->error==BLOCK_OK &&
        (s->chip_id_word&0xf00fffff)==0x10064345;
}
enum block_result brcm_sdio_read32(struct sdio_card *c,const struct brcm_sdio_state *s,uint32_t address,uint32_t *value)
{ return identified(c,s) ? read32(c,address,value) : BLOCK_NOT_READY; }
enum block_result brcm_sdio_write32(struct sdio_card *c,const struct brcm_sdio_state *s,uint32_t address,uint32_t value)
{
    if (!identified(c,s)) { return BLOCK_NOT_READY; }
    if ((address&3) || address<0x18000000 || address>0x181ffffc) { return BLOCK_ARGUMENT; }
    enum block_result r=window(c,address); if (r!=BLOCK_OK) { return r; }
    return sdio_write_word(c,(address&0x7fff)|0x8000,value);
}
enum block_result brcm_sdio_ram(struct sdio_card *c,const struct brcm_sdio_state *s,bool write,uint32_t address,void *data,size_t n)
{
    if (!identified(c,s)) { return BLOCK_NOT_READY; }
    if (!data || !n || n>512 || (n&3) || (address&3) || address<0x198000 || address>=0x260000 || n>0x260000-address) { return BLOCK_ARGUMENT; }
    unsigned char *p=data;
    while (n) {
        size_t amount=0x8000-(address&0x7fff); if (amount>n) { amount=n; }
        if (amount>BRCM_F1_BLOCK_SIZE) { amount=BRCM_F1_BLOCK_SIZE; }
        enum block_result r=window(c,address);if (r!=BLOCK_OK) { return r; }
        uint32_t offset=(address&0x7fff)|0x8000;
        r=write ? sdio_write_words(c,offset,p,amount) : sdio_read_bytes(c,1,offset,p,amount);
        if (r!=BLOCK_OK) { return r; }address+=(uint32_t)amount;p+=amount;n-=amount;
    }
    return BLOCK_OK;
}
enum block_result brcm_sdio_reset_vector(struct sdio_card *c,const struct brcm_sdio_state *s,uint32_t value)
{
    if (!identified(c,s)) { return BLOCK_NOT_READY; }
    if (!value || value==UINT32_MAX) { return BLOCK_ARGUMENT; }
    enum block_result r=window(c,0);if (r!=BLOCK_OK) { return r; }
    r=sdio_write_word(c,0x8000,value);if (r!=BLOCK_OK) { return r; }
    unsigned char data[4];r=sdio_read_bytes(c,1,0x8000,data,4);
    if (r!=BLOCK_OK) { return r; }
    uint32_t check=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
    return check==value ? BLOCK_OK : BLOCK_IO;
}

enum block_result brcm_sdio_packet(struct sdio_card *c,const struct brcm_sdio_state *s,bool write,void *data,size_t n)
{
    if (!identified(c,s)) { return BLOCK_NOT_READY; }
    if (!data || !n || n>(write ? 2048u : 512u) || (n&3) || c->functions<2) { return BLOCK_ARGUMENT; }
    enum block_result r=window(c,0x18000000);if (r!=BLOCK_OK) { return r; }
    for (size_t at=0;at<n;) {
        size_t amount=n-at;if (amount>512) { amount=512; }
        r=sdio_f2_packet_at(c,write,write ? 0x8000+(uint32_t)at : 0x8000,(unsigned char *)data+at,amount);
        if (r!=BLOCK_OK) { return r; }at+=amount;
    }return BLOCK_OK;
}
enum block_result brcm_sdio_identify(struct sdio_card *c,struct brcm_sdio_state *s)
{
    if (!c || !s) { return BLOCK_ARGUMENT; }
    *s=(struct brcm_sdio_state){.error=BLOCK_UNSUPPORTED};
    if (!c->selected || !c->have_identity || c->vendor!=0x02d0 || c->device!=0x4345 || c->functions<1) { return s->error; }
    uint8_t enabled; enum block_result r; s->phase=1;
    /* Configure the F1 FBR like brcmfmac before enabling the function. This is
     * independent of the host's per-command PIO block size. Read it back. */
    r=sdio_write_byte(c,0,0x110,BRCM_F1_BLOCK_SIZE);if (r!=BLOCK_OK) { goto done; }
    r=sdio_write_byte(c,0,0x111,0);if (r!=BLOCK_OK) { goto done; }
    uint8_t low,high;r=sdio_read_byte(c,0,0x110,&low);if (r!=BLOCK_OK) { goto done; }
    r=sdio_read_byte(c,0,0x111,&high);if (r!=BLOCK_OK) { goto done; }
    s->function1_block_size=(uint16_t)(low|(unsigned)high<<8);
    if (s->function1_block_size!=BRCM_F1_BLOCK_SIZE) { r=BLOCK_IO;goto done; }
    r=sdio_read_byte(c,0,2,&enabled); if (r!=BLOCK_OK) { goto done; }
    r=sdio_write_byte(c,0,2,enabled|2); if (r!=BLOCK_OK) { goto done; }
    uint64_t start=c->io.ticks(c->io.context);
    for (unsigned poll=0;;++poll) {
        uint8_t ready; r=sdio_read_byte(c,0,3,&ready); if (r!=BLOCK_OK) { goto done; }
        if (ready&2) { break; }
        if (poll==1000 || c->io.ticks(c->io.context)-start>=c->io.hz) { r=BLOCK_TIMEOUT; goto done; }
    }
    s->function1_ready=true; s->phase=2;
    /* Function-1 CHIPCLKCSR: hardware clock request off + ALP request. */
    r=sdio_write_byte(c,1,0x1000e,0x28); if (r!=BLOCK_OK) { goto done; }
    start=c->io.ticks(c->io.context);
    for (unsigned poll=0;;++poll) {
        r=sdio_read_byte(c,1,0x1000e,&s->clock_csr); if (r!=BLOCK_OK) { goto done; }
        if (s->clock_csr&0x40) { break; }
        if (poll==1000 || c->io.ticks(c->io.context)-start>=c->io.hz) { r=BLOCK_TIMEOUT; goto done; }
    }
    r=sdio_write_byte(c,1,0x1000e,0x21); if (r!=BLOCK_OK) { goto done; }
    uint64_t settle=c->io.ticks(c->io.context),delay=(c->io.hz*65+999999)/1000000;
    bool settled=false;
    for (unsigned spin=0;spin<1000000;++spin) {
        if (c->io.ticks(c->io.context)-settle>=delay) { settled=true; break; }
    }
    if (!settled) { r=BLOCK_TIMEOUT; goto done; }
    r=sdio_read_byte(c,1,0x1000e,&s->clock_csr); if (r!=BLOCK_OK) { goto done; }
    if (!(s->clock_csr&0x40)) { r=BLOCK_NOT_READY; goto done; }
    s->phase=3;
    r=read32(c,0x18000000,&s->chip_id_word); if (r!=BLOCK_OK) { goto done; }
    if (!(s->chip_id_word&0xffff) || (s->chip_id_word&0xffff)==0xffff) { r=BLOCK_IO; goto done; }
    s->chip_read=true; s->phase=4;
done:
    s->error=r; if (r!=BLOCK_OK) { c->selected=false; c->error=r; } return r;
}
