#include "sdio.h"
#define SDIO_ERRORS 0xffff8000u
static uint32_t rd(struct sdio_card *c,unsigned at) { return c->io.read(c->io.context,at); }
static void wr(struct sdio_card *c,unsigned at,uint32_t value) { c->io.write(c->io.context,at,value); }
static enum block_result irq(struct sdio_card *c,uint32_t mask)
{
    uint64_t start=c->io.ticks(c->io.context);
    for (unsigned spin=0;spin<10000000;++spin) {
        c->last_interrupt=rd(c,0x30);
        if (c->last_interrupt&SDIO_ERRORS) { return (c->last_interrupt&((1u<<16)|(1u<<20))) ? BLOCK_TIMEOUT : BLOCK_IO; }
        if (c->last_interrupt&mask) { wr(c,0x30,mask); return BLOCK_OK; }
        if (c->io.ticks(c->io.context)-start>=c->io.hz) { break; }
    }
    return BLOCK_TIMEOUT;
}
static enum block_result command(struct sdio_card *c,unsigned index,uint32_t argument,uint32_t flags,uint32_t mode)
{
    uint64_t start=c->io.ticks(c->io.context);
    for (unsigned spin=0;rd(c,0x24)&3;++spin) {
        if (spin>=1000000 || c->io.ticks(c->io.context)-start>=c->io.hz) { return BLOCK_TIMEOUT; }
    }
    wr(c,0x30,SDIO_ERRORS|0x23); wr(c,8,argument);
    wr(c,12,index<<24|flags<<16|mode);
    enum block_result r=irq(c,1);
    if (r==BLOCK_OK && flags) { c->last_response=rd(c,16); }
    return r;
}
enum block_result sdio_read_byte(struct sdio_card *c,unsigned function,uint32_t address,uint8_t *value)
{
    if (!c || !c->selected) { return BLOCK_NOT_READY; }
    if (!value || function>c->functions || address>0x1ffff) { return BLOCK_ARGUMENT; }
    enum block_result r=command(c,52,function<<28|address<<9,0x1a,0);
    if (r==BLOCK_OK && (c->last_response&0xcb00)) { r=BLOCK_IO; }
    if (r==BLOCK_OK) { *value=(uint8_t)c->last_response; }
    else { c->selected=false; }
    c->error=r; return r;
}
enum block_result sdio_read_bytes(struct sdio_card *c,unsigned function,uint32_t address,void *data,size_t n)
{
    if (!c || !c->selected) { return BLOCK_NOT_READY; }
    if (!data || !n || n>512 || function>c->functions || address>0x1ffff || n>0x20000-address) { return BLOCK_ARGUMENT; }
    /* Disable DMA and use one byte-mode block, with the requested byte count. */
    wr(c,4,(1u<<16)|(uint32_t)n);
    enum block_result r=command(c,53,function<<28|1u<<26|address<<9|((uint32_t)n&511),0x3a,0x10);
    if (r==BLOCK_OK && (c->last_response&0xcb00)) { r=BLOCK_IO; }
    if (r==BLOCK_OK) { r=irq(c,1u<<5); }
    if (r==BLOCK_OK) {
        unsigned char *bytes=data;
        for (size_t i=0;i<n;i+=4) {
            uint32_t value=rd(c,32);
            for (unsigned j=0;j<4 && i+j<n;++j) { bytes[i+j]=(unsigned char)(value>>(j*8)); }
        }
        r=irq(c,2);
    }
    if (r!=BLOCK_OK) { c->selected=false; } c->error=r; return r;
}
static enum block_result identity(struct sdio_card *c)
{
    uint32_t pointer=0;
    for (unsigned i=0;i<3;++i) {
        uint8_t value; enum block_result r=sdio_read_byte(c,0,9+i,&value);
        if (r!=BLOCK_OK) { return r; } pointer|=(uint32_t)value<<(i*8);
    }
    if (!pointer || pointer>0x1ffff) { return BLOCK_UNSUPPORTED; } c->cis=pointer;
    unsigned consumed=0;
    for (unsigned tuple=0;tuple<128 && consumed<1024;++tuple) {
        uint8_t type,length;
        enum block_result r=sdio_read_byte(c,0,pointer++,&type); ++consumed;
        if (r!=BLOCK_OK) { return r; }
        if (type==255) { return c->have_identity ? BLOCK_OK : BLOCK_UNSUPPORTED; }
        if (!type) { if (pointer>0x1ffff) { return BLOCK_RANGE; } continue; }
        if (pointer>0x1ffff) { return BLOCK_RANGE; }
        r=sdio_read_byte(c,0,pointer++,&length); ++consumed;
        if (r!=BLOCK_OK) { return r; }
        if (length==255 || pointer>0x1ffff || length>0x20000-pointer || length>1024-consumed) { return BLOCK_RANGE; }
        if (type==0x20) {
            if (length!=4 || c->have_identity) { return BLOCK_UNSUPPORTED; }
            uint8_t id[4];
            for (unsigned i=0;i<4;++i) {
                r=sdio_read_byte(c,0,pointer+i,&id[i]); if (r!=BLOCK_OK) { return r; }
            }
            c->vendor=(uint16_t)(id[0]|(unsigned)id[1]<<8); c->device=(uint16_t)(id[2]|(unsigned)id[3]<<8);
            c->have_identity=true;
        }
        pointer+=length; consumed+=length;
        if (pointer>0x1ffff) { return BLOCK_RANGE; }
    }
    return BLOCK_UNSUPPORTED;
}
enum block_result sdio_initialize(struct sdio_card *c,const struct sdhci_io *io)
{
    if (!c || !io || !io->read || !io->write || !io->ticks || !io->hz || io->hz>10000000000ULL) { return BLOCK_ARGUMENT; }
    *c=(struct sdio_card){.io=*io,.error=BLOCK_NOT_READY};
    uint32_t clock=rd(c,0x2c),control=rd(c,0x28);
    if ((clock&7)!=7 || (clock&0x07000000) || !(control&256) || (rd(c,0x24)&3) || (rd(c,0x208)&1)) { return c->error; }
    /* This candidate supports only an already configured 3.3V legacy bus. */
    if (((control>>9)&7)!=7 || (control&0x24) || (rd(c,0x3c)&0xf)) { c->error=BLOCK_UNSUPPORTED; return c->error; }
    c->changed=true; c->phase=SDIO_IDLE;
    wr(c,0x38,0); wr(c,0x34,SDIO_ERRORS|0x23);
    wr(c,0x2c,(clock&~0x000f0000u)|(14u<<16));
    enum block_result r=command(c,0,0,0,0); if (r!=BLOCK_OK) { goto done; }
    /* CMD0 resets card width; match it on the host without changing power. */
    wr(c,0x28,control&~2u);
    c->phase=SDIO_OCR;
    r=command(c,5,0,2,0); if (r!=BLOCK_OK) { goto done; }
    c->r4=c->last_response; c->functions=(uint8_t)((c->r4>>28)&7);
    uint32_t voltage=c->r4&0x00300000;
    if (!c->functions || !voltage || (c->r4&(1u<<27))) { r=BLOCK_UNSUPPORTED; goto done; }
    uint64_t start=io->ticks(io->context);
    for (unsigned attempt=0;;++attempt) {
        r=command(c,5,voltage,2,0); if (r!=BLOCK_OK) { goto done; } c->r4=c->last_response;
        if (c->r4&(1u<<31)) { break; }
        if (attempt==99 || io->ticks(io->context)-start>=io->hz) { r=BLOCK_TIMEOUT; goto done; }
        uint64_t pause=io->ticks(io->context);
        for (unsigned spin=0;spin<1000000 && io->ticks(io->context)-pause<io->hz/100;++spin) { }
    }
    c->phase=SDIO_RCA; r=command(c,3,0,0x1a,0); if (r!=BLOCK_OK) { goto done; }
    c->rca=(uint16_t)(c->last_response>>16);
    if (!c->rca || (c->last_response&0xe000)) { r=BLOCK_IO; goto done; }
    c->phase=SDIO_SELECT; r=command(c,7,(uint32_t)c->rca<<16,0x1b,0); if (r!=BLOCK_OK) { goto done; }
    if (c->last_response&0xfdffe008u) { r=BLOCK_IO; goto done; }
    r=irq(c,2); if (r!=BLOCK_OK) { goto done; } c->selected=true;
    c->phase=SDIO_CCCR; uint8_t revision;
    r=sdio_read_byte(c,0,0,&revision); if (r!=BLOCK_OK) { goto done; }
    c->cccr_revision=revision&15; c->sdio_revision=revision>>4;
    r=sdio_read_byte(c,0,7,&c->bus_control); if (r!=BLOCK_OK) { goto done; }
    /* CMD0 resets the card-side bus to 1 bit; reject mismatched inherited setup. */
    if (c->bus_control&3) { r=BLOCK_UNSUPPORTED; goto done; }
    c->phase=SDIO_CIS; r=identity(c); if (r!=BLOCK_OK) { goto done; }
    c->phase=SDIO_READY;
done:
    c->error=r; if (r!=BLOCK_OK) { c->selected=false; } return r;
}
