#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "brcm_chip.h"
#include "host_assert.h"
struct mock {
    uint32_t rom[BRCM_EROM_WORDS],pointer,bank;
    unsigned length,reads,writes,mode;
};
static uint32_t at(unsigned index) { return 0x18107000+index*4; }
static enum block_result read32(void *context,uint32_t address,uint32_t *value)
{
    struct mock *m=context; assert(++m->reads<1100);
    if (address==0x180000fc) { *value=m->pointer; return BLOCK_OK; }
    if (address>=at(0) && address<at(BRCM_EROM_WORDS) && !(address&3)) {
        if (m->mode==1) { return BLOCK_TIMEOUT; }
        *value=m->rom[(address-at(0))/4]; return BLOCK_OK;
    }
    switch (address) {
    case 0x18102408: *value=0x21; break;
    case 0x18102800: *value=m->mode==5 ? 1 : 0; break;
    case 0x18002004: *value=m->mode==6 ? 0 : m->mode==7 ? 0x33 : 0x22; break;
    case 0x18002040: *value=m->mode==4 && m->writes>=5 ? 0 : m->bank; break;
    case 0x18002044: {
        if (m->mode==2 && m->bank==1) { return BLOCK_IO; }
        const uint32_t bank[4]={7,31,0x201,15};
        if (m->mode==7) { *value=127; }
        else { assert(m->bank<4); *value=bank[m->bank]; }
        break;
    }
    default: return BLOCK_UNSUPPORTED;
    }
    return BLOCK_OK;
}
static enum block_result write32(void *context,uint32_t address,uint32_t value)
{
    struct mock *m=context;
    assert(address==0x18002040); assert(value<6 || value==7); ++m->writes;
    if (m->mode==3 && value==7) { return BLOCK_TIMEOUT; }
    m->bank=value; return BLOCK_OK;
}
static void component(struct mock *m,unsigned id,unsigned rev,uint32_t base,uint32_t wrapper,bool master)
{
    m->rom[m->length++]=0x4bf00001|(id<<8);
    m->rom[m->length++]=(rev<<24)|1|(wrapper ? (master ? 1u<<14|1u<<4 : 1u<<19) : 0)|(1u<<9);
    if (master) { m->rom[m->length++]=3; }
    m->rom[m->length++]=base|5;
    if (wrapper) { m->rom[m->length++]=wrapper|(master ? 0xc5 : 0x85); }
}
static void seed(struct mock *m)
{
    memset(m,0,sizeof(*m)); m->bank=7; m->pointer=at(0);
    component(m,0x800,54,0x18000000,0x18100000,false);
    component(m,0x812,42,0x18001000,0x18101000,true);
    component(m,0x83e,8,0x18002000,0x18102000,true);
    component(m,0x829,12,0x18005000,0x18105000,false);
    component(m,0x827,26,0x18006000,0,false);
    m->rom[m->length++]=15;
}
static void captured_prefix(struct mock *m)
{
    seed(m);
    /* First six words are verbatim Pi evidence. Wrapper and later components
     * are synthetic continuations to test that parsing proceeds past the size. */
    const uint32_t prefix[]={0x4bf80001,0x33004411,3,0x18000005,0x1c000135,0x01000000,0x181000c5};
    memmove(m->rom+7,m->rom+4,(m->length-4)*sizeof(m->rom[0]));
    memcpy(m->rom,prefix,sizeof(prefix)); m->length+=3;
}
static enum block_result discover(struct mock *m,struct brcm_chip_info *out)
{ struct brcm_chip_io io={read32,write32,m}; return brcm_chip_discover(&io,0x15264345,out); }
int main(void)
{
    struct mock m; struct brcm_chip_info out;
    seed(&m); assert(discover(&m,&out)==BLOCK_OK);
    assert(out.attempted && out.cores_valid && out.ram_valid && out.core_count==5);
    assert(out.core[0].id==0x800 && out.core[2].id==0x83e && out.core[2].wrapper==0x18102000);
    assert(out.banks==4 && out.ram_base==0x198000 && out.ram_bytes==450*1024);
    assert(out.bank_changed && out.bank_restored && m.bank==7 && m.writes==5);
    assert(!strcmp(brcm_core_name(0x83e),"ARM CR4"));
    struct brcm_chip_io io={read32,write32,&m}; unsigned before=m.reads;
    assert(brcm_chip_discover(&io,0x15254345,&out)==BLOCK_UNSUPPORTED && !out.attempted && m.reads==before);
    assert(brcm_chip_discover(&io,0x25264345,&out)==BLOCK_UNSUPPORTED && !out.attempted);
    assert(brcm_chip_discover(NULL,0,&out)==BLOCK_ARGUMENT);
    for (unsigned mode=1;mode<=7;++mode) {
        seed(&m); m.mode=mode; enum block_result e=discover(&m,&out);
        assert(e==(mode==1 || mode==3 ? BLOCK_TIMEOUT : mode==2 || mode==4 ? BLOCK_IO : mode==5 ? BLOCK_NOT_READY : mode==6 ? BLOCK_UNSUPPORTED : BLOCK_RANGE));
        assert(!out.ram_valid);
        if (mode==1 || mode==5 || mode==6) { assert(!m.writes); }
        if (mode==2 || mode==7) { assert(out.bank_restored && m.bank==7); }
        if (mode==3 || mode==4) { assert(!out.bank_restored && out.bank_changed); }
    }
    seed(&m); m.pointer=0x19000000; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && m.reads==1 && !m.writes);
    seed(&m); m.pointer|=1; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m); m.rom[1]=15; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m); m.rom[0]=15; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && !m.writes);
    seed(&m); m.rom[9]=0x4bf82a01; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && !m.writes); /* CM3 replacing required CR4 */
    seed(&m); m.rom[m.length-1]=0; assert(discover(&m,&out)==BLOCK_RANGE && !m.writes && out.rom_words==1024);
    seed(&m); m.rom[9]=0x4bf80001; assert(discover(&m,&out)==BLOCK_UNSUPPORTED && !m.writes); /* Duplicate ChipCommon */
    seed(&m); --m.length;
    for (unsigned i=0;i<13;++i) { component(&m,0x900+i,1,0x18010000+i*4096,0x18110000+i*4096,false); }
    m.rom[m.length++]=15; assert(discover(&m,&out)==BLOCK_RANGE && !m.writes && out.core_count==16);
    captured_prefix(&m); assert(discover(&m,&out)==BLOCK_OK && out.core_count==5 && out.ram_valid && m.bank==7);
    assert(out.rom_sample[4]==0x1c000135 && out.rom_sample[5]==0x01000000);
    /* The wide-size flag belongs to the raw size field; its high word must be
     * consumed before looking for the wrapper, regardless of descriptor tags. */
    captured_prefix(&m); memmove(m.rom+7,m.rom+6,(m.length-6)*sizeof(m.rom[0]));
    m.rom[5]=0x01000008; m.rom[6]=0; ++m.length;
    assert(discover(&m,&out)==BLOCK_OK && out.core_count==5 && out.bank_restored);
    captured_prefix(&m); m.rom[5]=0x01000008;
    for (unsigned i=6;i<BRCM_EROM_WORDS;++i) { m.rom[i]=0; }
    assert(discover(&m,&out)==BLOCK_RANGE && !m.writes && out.rom_words==1024);
    /* 64-bit and explicit-size address payloads are consumed, never misread
     * as component headers. These extra regions are outside required cores. */
    seed(&m); unsigned end=m.length-1;
    const uint32_t extra[]={0x4bf90001,1u<<19|1,0x1800a00d,0,0x1810a085,0x1800b035,0x2000,15};
    memcpy(m.rom+end,extra,sizeof(extra)); m.length=end+sizeof(extra)/4;
    assert(discover(&m,&out)==BLOCK_OK && out.core_count==6);
    struct mock original,physical_prefix; seed(&original); captured_prefix(&physical_prefix); uint32_t random=0x657381ab;
    for (unsigned i=0;i<10000;++i) {
        m=(i&1) ? physical_prefix : original; random=random*1664525+1013904223;
        unsigned index=random%m.length; random=random*1664525+1013904223;
        m.rom[index]^=1u<<(random%32);
        (void)discover(&m,&out); assert(m.reads<1100);
        if (!out.cores_valid) { assert(!m.writes); }
        if (out.ram_valid) { assert(out.cores_valid && out.bank_restored && m.bank==7); }
    }
    puts("PASS: AI EROM core/wrapper discovery, wide/size payloads, missing/invalid/truncated tables, exact CR4 bank-only writes, measured sizing, restoration/error paths, chip gating and 10000 ROM mutations");
    puts("PASS: captured Pi EROM prefix with untagged 0x01000000 size, wide raw size, bounded truncation and continuation to RAM query");
    return 0;
}
