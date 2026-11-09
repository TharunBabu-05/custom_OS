#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xhci_test.h"
void ring_tests(void);
static struct xhci_test_memory memory;
struct model {
    uint32_t reg[0x5000/4],writes;
    uint64_t time;
    unsigned mode;
};
static bool rd(void *context,uint32_t offset,uint32_t *value)
{ struct model *m=context; assert(!(offset&3) && offset<sizeof(m->reg)); *value=m->reg[offset/4]; return true; }
static bool map(void *context,const void *p,size_t n,uint64_t *bus)
{
    struct model *m=context;
    if (m->mode==1) { return false; }
    assert((uintptr_t)p>=(uintptr_t)&memory && (uintptr_t)p-(uintptr_t)&memory+n<=sizeof(memory));
    *bus=0x1000000000ULL+(uintptr_t)p-(uintptr_t)&memory; return true;
}
static uint64_t pair(const struct model *m,unsigned offset)
{ return (uint64_t)m->reg[offset/4+1]<<32|m->reg[offset/4]; }
static bool wr(void *context,uint32_t offset,uint32_t value)
{
    struct model *m=context; ++m->writes;
    assert(offset==0x40 || offset==0x44 || offset==0x58 || offset==0x5c || offset==0x70 || offset==0x74 || offset==0x78 ||
        offset==0x2020 || offset==0x2024 || offset==0x2028 || offset==0x2030 || offset==0x2034 || offset==0x2038 || offset==0x203c || offset==0x1000);
    if (offset==0x40) {
        if (value&2) {
            if (m->mode==3) { m->reg[offset/4]=value; return true; }
            m->reg[offset/4]=0; m->reg[0x44/4]=1; return true;
        }
        assert(!(value&12)); /* Interrupt enables stay off. */
        m->reg[offset/4]=value;
        if (m->mode!=2) { m->reg[0x44/4]=(value&1) ? 0 : 1; }
        return true;
    }
    if (offset==0x44) { m->reg[offset/4]&=~value; return true; }
    m->reg[offset/4]=value;
    if (offset==0x1000) {
        assert(value==0 && m->reg[0x40/4]==1);
        assert(pair(m,0x58)==(0x1000000000ULL|1));
        assert(pair(m,0x70)==0x1000000000ULL+offsetof(struct xhci_test_memory,dcbaa));
        assert(pair(m,0x2030)==0x1000000000ULL+offsetof(struct xhci_test_memory,erst));
        assert(memory.erst.base==0x1000000000ULL+offsetof(struct xhci_test_memory,event) && memory.erst.size==256);
        assert(memory.dcbaa[0]==0x1000000000ULL+offsetof(struct xhci_test_memory,scratch_array));
        for (unsigned i=0;i<2;++i) { assert(memory.scratch_array[i]==0x1000000000ULL+offsetof(struct xhci_test_memory,scratch)+i*4096); }
        if (m->mode==4) { return true; } /* No DMA completion. */
        unsigned at=0;
        if (m->mode==7) { memory.event[at++]=(struct xhci_trb){1u<<24,0,0,(34u<<10)|1}; }
        for (unsigned i=0;i<4;++i) {
            assert(memory.command[i].control==((23u<<10)|1));
            uint64_t pointer=0x1000000000ULL+i*16;
            if (m->mode==5) { pointer+=16; }
            memory.event[at++]=(struct xhci_trb){(uint32_t)pointer,(uint32_t)(pointer>>32),
                (m->mode==6 ? 5u : 1u)<<24,(33u<<10)|1};
        }
    }
    return true;
}
static void barrier(void *context) { (void)context; }
static uint64_t ticks(void *context) { return ++((struct model *)context)->time; }
static void seed(struct model *m)
{
    memset(m,0,sizeof(*m)); m->reg[0]=0x01100040; m->reg[1]=(3u<<24)|(4u<<8)|64;
    m->reg[2]=2u<<27; m->reg[4]=5; m->reg[5]=0x1000; m->reg[6]=0x2000;
    m->reg[0x40/4]=13; m->reg[0x44/4]=0x18; m->reg[0x48/4]=1;
}
static void dma_tests(void)
{
    struct model m={0}; struct xhci_observer bridge={rd,&m,sizeof(m.reg)}; struct pi5_dma_window window;
    m.reg[0x4034/4]=19; m.reg[0x4038/4]=16; m.reg[0x40b4/4]=1;
    assert(pi5_dma_window_read(&bridge,&window) && window.size==(1ULL<<34));
    uint64_t address;
    assert(pi5_dma_address(&window,0x80000,4096,&address) && address==0x1000080000ULL);
    assert(!pi5_dma_address(&window,window.size-1,2,&address));
    assert(!pi5_dma_address(&window,UINT64_MAX,2,&address));
    m.reg[0x40b4/4]=0; assert(!pi5_dma_window_read(&bridge,&window));
    m.reg[0x40b4/4]=1; m.reg[0x4034/4]=0; assert(!pi5_dma_window_read(&bridge,&window));
    m.reg[0x4034/4]=19; m.reg[0x4038/4]=0; assert(!pi5_dma_window_read(&bridge,&window));
    m.reg[0x4038/4]=16; m.reg[0x188/4]=4; assert(!pi5_dma_window_read(&bridge,&window));
    FILE *f=fopen(".dist/rpi5-firmware/bcm2712-rpi-5-b.dtb","rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); rewind(f); unsigned char *data=malloc((size_t)n);
    assert(data && fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
    assert(pi5_dma_description(data,(size_t)n)); assert(!pi5_dma_description(data,(size_t)n-1)); free(data);
    puts("PASS: live BAR2/remap decoding, RP1 DMA description, mapped bounds and invalid-window rejection");
}
void ring_tests(void)
{
    struct model m; seed(&m);
    struct xhci_test_io io={{rd,&m,sizeof(m.reg)},wr,map,barrier,ticks,100}; struct xhci_test_state state={0};
    assert(xhci_ring_test(&io,&memory,&state)==XHCI_TEST_OK && state.completed==4 && state.halted);
    unsigned writes=m.writes; assert(xhci_ring_test(&io,&memory,&state)==XHCI_TEST_USED && m.writes==writes);
    for (unsigned mode=1;mode<=7;++mode) {
        seed(&m); m.mode=mode; state=(struct xhci_test_state){0};
        enum xhci_test_result r=xhci_ring_test(&io,&memory,&state);
        if (mode==1) { assert(r==XHCI_TEST_DMA && !m.writes && !state.attempted); }
        else if (mode<=4) { assert(r==XHCI_TEST_TIMEOUT && state.attempted); }
        else if (mode<=6) { assert(r==XHCI_TEST_EVENT && state.halted); }
        else { assert(r==XHCI_TEST_OK && state.completed==4 && state.halted); }
    }
    seed(&m); state=(struct xhci_test_state){0}; m.reg[2]=17u<<27;
    assert(xhci_ring_test(&io,&memory,&state)==XHCI_TEST_LAYOUT && !m.writes);
    seed(&m); m.reg[4]|=0x100u<<16; m.reg[0x400/4]=1|(1u<<16); state=(struct xhci_test_state){0};
    assert(xhci_ring_test(&io,&memory,&state)==XHCI_TEST_LEGACY && !m.writes);
    puts("PASS: exact xHCI halt/reset/setup/No-op engine, DMA and scratchpad wiring, completion matching, timeout/error cleanup and retry refusal");
    dma_tests();
}
