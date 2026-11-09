#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdio.h"
struct model {
    uint32_t reg[256]; unsigned char function0[0x20000];
    unsigned writes,commands,position,mode; uint64_t time;
};
static struct model model;
static uint32_t rd(void *context,unsigned offset)
{
    struct model *m=context; assert(!(offset&3) && offset<sizeof(m->reg));
    if (offset==32) {
        uint32_t value=0; unsigned address=(m->reg[8/4]>>9)&0x1ffff;
        unsigned n=m->reg[4/4]&4095;
        for (unsigned i=0;i<4 && m->position<n;++i) { value|=(uint32_t)m->function0[address+m->position++]<<(i*8); }
        return value;
    }
    return m->reg[offset/4];
}
static void wr(void *context,unsigned offset,uint32_t value)
{
    struct model *m=context; ++m->writes;
    assert(offset==4 || offset==8 || offset==12 || offset==0x28 || offset==0x2c || offset==0x30 || offset==0x34 || offset==0x38);
    if (offset==0x28) { assert(value==0xf00); } /* Power preserved, 1-bit width. */
    if (offset==0x30) { m->reg[offset/4]&=~value; return; }
    m->reg[offset/4]=value;
    if (offset!=12) { return; } ++m->commands;
    unsigned cmd=value>>24,arg=m->reg[8/4]; m->reg[0x30/4]=1;
    if (m->mode==1) { m->reg[0x30/4]=1u<<16; return; }
    switch (cmd) {
    case 0: assert(arg==0 && (value&0xffff)==0); m->function0[7]=0; break;
    case 5: assert(!(arg&~0x00300000)); m->reg[16/4]=m->mode==3 ? 0x20300000 : 0xa0300000; break;
    case 3: assert(!arg); m->reg[16/4]=0x12340000; break;
    case 7: assert(arg==0x12340000); m->reg[16/4]=0; m->reg[0x30/4]|=2; break;
    case 52:
        assert(!(arg&0x80000000)); /* Every CMD52 is a read. */
        m->reg[16/4]=m->mode==2 ? 0x8000 : m->function0[arg>>9&0x1ffff]; break;
    case 53:
        assert(!(arg&0x80000000) && !(arg&(1u<<27)) && (arg&(1u<<26)) && (value&0xffff)==16);
        m->reg[16/4]=0; m->position=0; m->reg[0x30/4]|=32|2;
        if (m->mode==5) { m->reg[0x30/4]=1u<<21; } break;
    default: assert(!"Unexpected SDIO command");
    }
}
static uint64_t ticks(void *context) { return ++((struct model *)context)->time; }
static void seed(void)
{
    memset(&model,0,sizeof(model)); model.reg[0x28/4]=0xf02; model.reg[0x2c/4]=0x0107;
    model.function0[0]=0x43; model.function0[10]=0x10;
    const unsigned char cis[]={0x20,4,0xd0,2,0xbf,0xa9,0xff}; memcpy(model.function0+0x1000,cis,sizeof(cis));
}
int main(void)
{
    struct sdhci_io io={rd,wr,ticks,&model,1000}; struct sdio_card card;
    seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK && card.selected && card.phase==SDIO_READY);
    assert(card.functions==2 && card.rca==0x1234 && card.vendor==0x2d0 && card.device==0xa9bf);
    assert(card.cccr_revision==3 && card.sdio_revision==4 && model.reg[0x28/4]==0xf00);
    unsigned char data[512]; assert(sdio_read_bytes(&card,0,card.cis,data,64)==BLOCK_OK);
    assert(!memcmp(data,model.function0+0x1000,64));
    assert(sdio_read_bytes(&card,0,card.cis,data,1)==BLOCK_OK && data[0]==0x20);
    assert(sdio_read_bytes(&card,0,card.cis,data,512)==BLOCK_OK && (model.reg[8/4]&511)==0);
    unsigned writes=model.writes;
    assert(sdio_read_bytes(&card,0,0x1ffff,data,2)==BLOCK_ARGUMENT && model.writes==writes);
    assert(sdio_read_bytes(&card,3,0,data,1)==BLOCK_ARGUMENT && model.writes==writes);
    assert(sdio_read_bytes(&card,0,0,data,0)==BLOCK_ARGUMENT && model.writes==writes);
    seed(); model.reg[0x2c/4]=0; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    seed(); model.reg[0x28/4]=0; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    seed(); model.reg[0x208/4]=1; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    for (unsigned mode=1;mode<=3;++mode) {
        seed(); model.mode=mode;
        assert(sdio_initialize(&card,&io)==(mode==2 ? BLOCK_IO : BLOCK_TIMEOUT) && !card.selected);
    }
    seed(); model.function0[0x1001]=255;
    assert(sdio_initialize(&card,&io)==BLOCK_RANGE && !card.selected);
    seed(); model.function0[10]=255; model.function0[11]=255;
    assert(sdio_initialize(&card,&io)==BLOCK_UNSUPPORTED && !card.selected);
    seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK); model.mode=5;
    assert(sdio_read_bytes(&card,0,card.cis,data,64)==BLOCK_IO && !card.selected);
    writes=model.writes; assert(sdio_read_bytes(&card,0,0,data,64)==BLOCK_NOT_READY && model.writes==writes);
    puts("PASS: SDIO CMD0/5/3/7 selection, CMD52 reads, CCCR/CIS identity, read-only CMD53 lengths/bounds, errors and fail-closed power/clock/CQE checks");
    return 0;
}
