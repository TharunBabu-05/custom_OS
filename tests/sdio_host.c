#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdio.h"
#include "brcm_sdio.h"
#include "host_assert.h"
struct model {
    uint32_t reg[256]; unsigned char function0[0x20000],function1[0x20000];
    unsigned char ram[819200];
    unsigned char function2[2048];
    unsigned writes,commands,extended,position,mode,fifo_writes,max_ram_wire; uint64_t time;
};
static struct model model;
static unsigned char *fifo_address(struct model *m,unsigned function,unsigned address,unsigned position)
{
    uint32_t window=(uint32_t)m->function1[0x1000a]<<8|(uint32_t)m->function1[0x1000b]<<16|(uint32_t)m->function1[0x1000c]<<24;
    uint32_t target=window+(address&0x7fff)+position;
    if (function==2) { assert(address>=0x8000 && address<0x8800 && window==0x18000000 && position<512);unsigned at=(m->reg[8/4]>>31 ? address-0x8000 : 0)+position;assert(at<2048);return m->function2+at; }
    if (function==1 && address>=0x8000 && address<0x10000 && window>=0x198000 && window<0x260000) {
        assert(target>=0x198000 && target<0x260000);return m->ram+target-0x198000;
    }
    assert(address+position<0x20000);return (function ? m->function1 : m->function0)+address+position;
}
static uint32_t rd(void *context,unsigned offset)
{
    struct model *m=context; assert(!(offset&3) && offset<sizeof(m->reg));
    if (offset==32) {
        uint32_t value=0; unsigned address=(m->reg[8/4]>>9)&0x1ffff;
        unsigned n=m->reg[4/4]&4095;
        for (unsigned i=0;i<4 && m->position<n;++i) {
            value|=(uint32_t)*fifo_address(m,m->reg[8/4]>>28&7,address,m->position)<<(i*8);++m->position;
        }
        if (m->mode==15 && !m->function1[0x1000a] && !m->function1[0x1000b] && !m->function1[0x1000c]) { value^=1; }
        return value;
    }
    return m->reg[offset/4];
}
static void wr(void *context,unsigned offset,uint32_t value)
{
    struct model *m=context; ++m->writes;
    assert(offset==4 || offset==8 || offset==12 || offset==32 || offset==0x28 || offset==0x2c || offset==0x30 || offset==0x34 || offset==0x38);
    if (offset==32) {
        ++m->fifo_writes;
        assert((m->reg[8/4]>>31)==1 && ((m->reg[8/4]>>28&7)==1 || (m->reg[8/4]>>28&7)==2));
        unsigned address=m->reg[8/4]>>9&0x1ffff;
        assert(m->position+4<=(m->reg[4/4]&4095));
        for (unsigned i=0;i<4;++i) { *fifo_address(m,m->reg[8/4]>>28&7,address,m->position)=(unsigned char)(value>>(i*8));++m->position; }
        if (m->mode==14 && m->position==8) { m->reg[0x30/4]|=1u<<21; }
        return;
    }
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
    case 52: {
        unsigned function=arg>>28&7,address=arg>>9&0x1ffff;
        assert(function<=1); unsigned char *bytes=function ? m->function1 : m->function0;
        if (arg&0x80000000) {
            assert((!function && (address==2 || address==0x110 || address==0x111)) || (function && address>=0x1000a && address<=0x1000e));
            bytes[address]=(unsigned char)arg;
            if (m->mode==12 && !function && address==0x110) { bytes[address]=0; }
            if (!function && address==2 && m->mode!=6) { bytes[3]=(unsigned char)(arg&7); }
            if (function && address==0x1000e && m->mode!=7) { bytes[address]|=0x40; }
        }
        m->reg[16/4]=m->mode==2 ? 0x8000 : bytes[address]; break;
    }
    case 53:
        assert(!(arg&(1u<<27)) && ((arg>>26&1)==((arg>>28&7)==2 ? arg>>31 : 1)) && (value&0xffff)==((arg>>31) ? 2 : 18));
        ++m->extended;
        if (m->mode==16 && (arg>>28&7)==2 && (arg>>9&0x1ffff)==0x8200) { m->reg[16/4]=0x8000;return; }
        if (m->function1[0x1000c]==0 && m->function1[0x1000b]>=0x19 && m->function1[0x1000b]<=0x25) {
            unsigned wire=m->reg[4/4]&4095;
            assert(m->function0[0x110]==64 && m->function0[0x111]==0);
            if (wire>m->max_ram_wire) { m->max_ram_wire=wire; }
            if (m->mode==13 && wire>64) { m->reg[16/4]=0x1100;m->reg[0x30/4]=0x11;return; }
        }
        if ((arg>>28&7)==1 && (arg>>9&0x1ffff)>=0x8000) { assert(!(m->function1[0x1000a]&0x7f)); }
        m->reg[16/4]=m->mode==9 ? 0x8000 : m->mode==8 && !(arg>>28&7) ? 0x1100 : 0;
        m->position=0; m->reg[0x30/4]|=(m->mode==10 ? 0 : (arg>>31) ? 16 : 32)|(m->mode==11 ? 0 : 2);
        if (m->mode==5) { m->reg[0x30/4]=1u<<21; } break;
    default: assert(!"Unexpected SDIO command");
    }
}
static uint64_t ticks(void *context) { return ++((struct model *)context)->time; }
static void seed(void)
{
    memset(&model,0,sizeof(model)); model.reg[0x28/4]=0xf02; model.reg[0x2c/4]=0x0107;
    model.function0[0]=0x43; model.function0[10]=0x10;
    const unsigned char cis[]={0x20,4,0xd0,2,0x45,0x43,0xff}; memcpy(model.function0+0x1000,cis,sizeof(cis));
    const unsigned char chip[]={0x45,0x43,0x06,0x16}; memcpy(model.function1+0x8000,chip,4);
}
int main(void)
{
    struct sdhci_io io={rd,wr,ticks,&model,1000}; struct sdio_card card;
    seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK && card.selected && card.phase==SDIO_READY);
    assert(card.functions==2 && card.rca==0x1234 && card.vendor==0x2d0 && card.device==0x4345);
    assert(card.cccr_revision==3 && card.sdio_revision==4 && model.reg[0x28/4]==0xf00);
    unsigned char data[512]; assert(sdio_read_bytes(&card,0,card.cis,data,64)==BLOCK_OK);
    assert(!memcmp(data,model.function0+0x1000,64));
    assert(sdio_read_bytes(&card,0,card.cis,data,1)==BLOCK_OK && data[0]==0x20);
    assert(sdio_read_bytes(&card,1,0x1000,data,512)==BLOCK_OK && (model.reg[8/4]&511)==0);
    struct brcm_sdio_state chip;
    assert(brcm_sdio_identify(&card,&chip)==BLOCK_OK && chip.function1_ready && chip.chip_read && chip.chip_id_word==0x16064345);
    assert(chip.function1_block_size==64 && model.function0[0x110]==64 && model.function0[0x111]==0);
    uint32_t word; assert(brcm_sdio_read32(&card,&chip,0x18000000,&word)==BLOCK_OK && word==0x16064345);
    assert(brcm_sdio_write32(&card,&chip,0x18002040,0x11223344)==BLOCK_OK);
    assert(brcm_sdio_read32(&card,&chip,0x18002040,&word)==BLOCK_OK && word==0x11223344);
    /* Crossing a 32 KiB window changes all three address bytes correctly. */
    assert(brcm_sdio_write32(&card,&chip,0x18108040,0x55667788)==BLOCK_OK);
    assert(model.function1[0x1000a]==0x80 && model.function1[0x1000b]==0x10 && model.function1[0x1000c]==0x18);
    for (unsigned i=0;i<512;++i) { data[i]=(unsigned char)i; }
    model.mode=13;unsigned before_ram=model.extended;
    assert(brcm_sdio_ram(&card,&chip,true,0x198000,data,512)==BLOCK_OK && model.extended==before_ram+8);
    memset(data,0,512);assert(brcm_sdio_ram(&card,&chip,false,0x198000,data,512)==BLOCK_OK);
    for (unsigned i=0;i<512;++i) { assert(data[i]==(unsigned char)i); }
    assert(model.max_ram_wire==64);
    for (unsigned offset=0;offset<sizeof(model.ram);offset+=512) {
        for (unsigned i=0;i<512;++i) { unsigned p=offset+i;data[i]=(unsigned char)((p*13)^(p>>8)^(p>>16)); }
        assert(brcm_sdio_ram(&card,&chip,true,0x198000+offset,data,512)==BLOCK_OK);
    }
    for (unsigned offset=0;offset<sizeof(model.ram);offset+=512) {
        assert(brcm_sdio_ram(&card,&chip,false,0x198000+offset,data,512)==BLOCK_OK);
        for (unsigned i=0;i<512;++i) { unsigned p=offset+i;assert(data[i]==(unsigned char)((p*13)^(p>>8)^(p>>16))); }
    }
    assert(model.max_ram_wire==64 && !card.failure.valid);model.mode=0;
    for (unsigned i=0;i<512;++i) { data[i]=(unsigned char)(i^0x5a); }
    assert(brcm_sdio_packet(&card,&chip,true,data,284)==BLOCK_OK);
    assert((card.last_argument>>28&7)==2 && (card.last_argument&(1u<<26)) && card.fifo_bytes==284);
    assert(!memcmp(data,model.function2,284));
    memset(data,0,512);assert(brcm_sdio_packet(&card,&chip,false,data,284)==BLOCK_OK);
    assert((card.last_argument>>28&7)==2 && !(card.last_argument&(1u<<26)) && card.fifo_bytes==284);
    for (unsigned i=0;i<284;++i) { assert(data[i]==(unsigned char)(i^0x5a)); }
    for (unsigned i=0;i<512;++i) { data[i]=(unsigned char)(i^0x5a); }
    assert(brcm_sdio_packet(&card,&chip,true,data,512)==BLOCK_OK && !(card.last_argument&511));
    memset(data,0,512);assert(brcm_sdio_packet(&card,&chip,false,data,512)==BLOCK_OK && !(card.last_argument&511));
    for (unsigned i=0;i<512;++i) { assert(data[i]==(unsigned char)(i^0x5a)); }
    unsigned char large[1448];for (unsigned i=0;i<sizeof(large);++i) { large[i]=(unsigned char)(i*17); }
    assert(brcm_sdio_packet(&card,&chip,true,large,sizeof(large))==BLOCK_OK);
    assert(!memcmp(large,model.function2,sizeof(large)) && (card.last_argument>>9&0x1ffff)==0x8400 && card.transfer_bytes==424);
    unsigned before_f2_bad=model.writes;
    assert(brcm_sdio_packet(&card,&chip,true,data,3)==BLOCK_ARGUMENT && model.writes==before_f2_bad);
    assert(brcm_sdio_reset_vector(&card,&chip,0xb83ef198)==BLOCK_OK);
    assert(model.function1[0x1000a]==0 && model.function1[0x1000b]==0 && model.function1[0x1000c]==0);
    assert(brcm_sdio_read32(&card,&chip,0,&word)==BLOCK_ARGUMENT);
    assert(brcm_sdio_ram(&card,&chip,true,0,data,4)==BLOCK_ARGUMENT);
    assert(brcm_sdio_reset_vector(&card,&chip,0)==BLOCK_ARGUMENT);
    assert(brcm_sdio_reset_vector(&card,&chip,UINT32_MAX)==BLOCK_ARGUMENT);
    model.mode=15;assert(brcm_sdio_reset_vector(&card,&chip,0xb83ef198)==BLOCK_IO);model.mode=0;
    unsigned extended=model.extended;
    assert(brcm_sdio_ram(&card,&chip,true,0x19fffc,data,8)==BLOCK_OK && model.extended==extended+2);
    assert(model.function1[0x1000a]==0 && model.function1[0x1000b]==0x1a && model.function1[0x1000c]==0);
    unsigned before_bad=model.writes;
    assert(brcm_sdio_read32(&card,&chip,0x18000001,&word)==BLOCK_ARGUMENT);
    assert(brcm_sdio_write32(&card,&chip,0x00198000,0)==BLOCK_ARGUMENT);
    assert(sdio_write_word(&card,0x1ffff,0)==BLOCK_ARGUMENT && model.writes==before_bad);
    assert(brcm_sdio_ram(&card,&chip,true,0x260000,data,4)==BLOCK_ARGUMENT && model.writes==before_bad);
    assert(brcm_sdio_ram(&card,&chip,true,0x25fffc,data,8)==BLOCK_ARGUMENT && model.writes==before_bad);
    assert(sdio_write_words(&card,0x8000,data,3)==BLOCK_ARGUMENT && model.writes==before_bad);
    unsigned before_unknown=model.writes; card.device=0x1234;
    assert(brcm_sdio_identify(&card,&chip)==BLOCK_UNSUPPORTED && model.writes==before_unknown); card.device=0x4345;
    unsigned writes=model.writes;
    assert(sdio_read_bytes(&card,0,0x1ffff,data,2)==BLOCK_ARGUMENT && model.writes==writes);
    assert(sdio_read_bytes(&card,3,0,data,1)==BLOCK_ARGUMENT && model.writes==writes);
    assert(sdio_read_bytes(&card,0,0,data,0)==BLOCK_ARGUMENT && model.writes==writes);
    seed(); model.reg[0x2c/4]=0; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    seed(); model.reg[0x28/4]=0; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    seed(); model.reg[0x208/4]=1; assert(sdio_initialize(&card,&io)==BLOCK_NOT_READY && !model.writes);
    seed(); model.reg[0x3c/4]=1u<<19; assert(sdio_initialize(&card,&io)==BLOCK_UNSUPPORTED && !model.writes);
    for (unsigned mode=1;mode<=3;++mode) {
        seed(); model.mode=mode;
        assert(sdio_initialize(&card,&io)==(mode==2 ? BLOCK_IO : BLOCK_TIMEOUT) && !card.selected);
    }
    seed(); model.function0[0x1001]=255;
    assert(sdio_initialize(&card,&io)==BLOCK_RANGE && !card.selected);
    seed(); model.function0[10]=255; model.function0[11]=255;
    assert(sdio_initialize(&card,&io)==BLOCK_UNSUPPORTED && !card.selected);
    seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK); model.mode=5;
    assert(sdio_read_bytes(&card,1,0x1000,data,64)==BLOCK_IO && !card.selected);
    writes=model.writes; assert(sdio_read_bytes(&card,0,0,data,64)==BLOCK_NOT_READY && model.writes==writes);
    seed(); model.mode=8; assert(sdio_initialize(&card,&io)==BLOCK_OK);
    assert(sdio_read_bytes(&card,0,card.cis,data,64)==BLOCK_OK && card.selected && !model.extended);
    for (unsigned mode=6;mode<=7;++mode) {
        seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK); model.mode=mode;
        assert(brcm_sdio_identify(&card,&chip)==BLOCK_TIMEOUT && !card.selected);
    }
    const unsigned failures[]={1,5,9,10,11};
    for (unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);++i) {
        seed(); assert(sdio_initialize(&card,&io)==BLOCK_OK); model.mode=failures[i];
        assert(sdio_write_word(&card,0x9040,0x12345678)==(model.mode==5 || model.mode==9 ? BLOCK_IO : BLOCK_TIMEOUT));
        if (card.selected || model.fifo_writes!=(model.mode==11 ? 1u : 0u)) {
            fprintf(stderr,"write failure mode=%u selected=%u FIFO writes=%u\n",model.mode,card.selected,model.fifo_writes);
            assert(0);
        }
        writes=model.writes; assert(sdio_write_word(&card,0x9040,0)==BLOCK_NOT_READY && model.writes==writes);
        assert(card.failure.valid && card.failure.command==53 && card.failure.bytes==4);
        struct sdio_failure saved=card.failure;
        assert(sdio_read_bytes(&card,1,0x9040,data,4)==BLOCK_NOT_READY && !memcmp(&saved,&card.failure,sizeof(saved)));
    }
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);model.mode=12;
    assert(brcm_sdio_identify(&card,&chip)==BLOCK_IO && !card.selected && !chip.function1_block_size && !model.function0[2]);
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);model.mode=14;
    assert(sdio_write_words(&card,0x9040,data,64)==BLOCK_IO && model.fifo_writes==2);
    assert(card.failure.valid && card.failure.stage==SDIO_PIO && card.failure.fifo_bytes==8 && card.failure.bytes==64 && card.failure.interrupt==(2|(1u<<21)));
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);assert(brcm_sdio_identify(&card,&chip)==BLOCK_OK);
    /* Set the RAM window via a supported access, then reproduce the captured
     * response to a raw 512-byte/zero-count request. No FIFO data may follow. */
    assert(brcm_sdio_ram(&card,&chip,false,0x198000,data,4)==BLOCK_OK);model.mode=13;
    assert(sdio_write_words(&card,0x8000,data,512)==BLOCK_IO && !model.fifo_writes);
    assert(card.failure.valid && card.failure.response==0x1100 && card.failure.interrupt==0x11 && !card.failure.fifo_bytes && card.failure.bytes==512);
    puts("PASS: SDIO CMD0/5/3/7 selection, CMD52 reads, CCCR/CIS identity, read-only CMD53 lengths/bounds, errors and fail-closed power/clock/CQE checks");
    puts("PASS: captured function-0 R5 rejection avoided, function-1 enable/ALP/backplane ID and readiness timeouts");
    puts("PASS: CMD53 function-1 aligned word write/read, window remapping, argument/R5/CRC/command/buffer/transfer timeout checks and no access after failure");
    puts("PASS: F1 block-size setup/readback, explicit host block count, 64-byte RAM wire limit, PIO mid-block error stop and preserved first-failure snapshot");
    puts("PASS: full 800 KiB SDIO/PIO pattern written then read across all backplane windows under the 64-byte wire constraint");
    puts("PASS: captured R5=0x1100/IRQ=0x11 rejection stops before FIFO writes; split RAM path avoids the rejected 512-byte request");
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);assert(brcm_sdio_identify(&card,&chip)==BLOCK_OK);model.mode=9;
    assert(brcm_sdio_packet(&card,&chip,true,data,32)==BLOCK_IO && !card.selected && !model.fifo_writes);
    assert(card.failure.valid && (card.failure.argument>>28&7)==2 && !card.failure.fifo_bytes);
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);assert(brcm_sdio_identify(&card,&chip)==BLOCK_OK);model.mode=14;
    assert(brcm_sdio_packet(&card,&chip,true,data,32)==BLOCK_IO && card.failure.fifo_bytes==8);
    puts("PASS: F2 packet PIO, incrementing TX/fixed RX, ChipCommon window, aligned bounds, R5 refusal and mid-FIFO CRC stop");
    seed();assert(sdio_initialize(&card,&io)==BLOCK_OK);assert(brcm_sdio_identify(&card,&chip)==BLOCK_OK);model.mode=16;
    assert(brcm_sdio_packet(&card,&chip,true,large,sizeof(large))==BLOCK_IO && model.fifo_writes==128);
    assert(card.failure.valid && (card.failure.argument>>9&0x1ffff)==0x8200 && !card.failure.fifo_bytes && !model.function2[513]);
    puts("PASS: multi-command F2 TX continues addresses and stops before a rejected second fragment");
    return 0;
}
