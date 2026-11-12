/* Inject real transfer/command events into the production transport to
 * verify host/device halt clearing, exact dequeue/cycle and error handling. */
#include "host_assert.h"
#include <string.h>
#include "../drivers/usb/xhci_enumerate.c"
void console_write(const char *s) { (void)s; }
void console_u64(uint64_t x) { (void)x; }
void console_hex(uint64_t x) { (void)x; }
static struct xhci_test_memory memory;
static struct xhci_test_state state;
struct model {
    unsigned mode,produced,next_command,next_ep,step,endpoint,bulk_calls;
    uint64_t time;bool stall,stopped;
};
static uint64_t address(const void *p) { return 0x100000000ULL+(uintptr_t)p-(uintptr_t)&memory; }
static void emit(struct model *m,uint64_t ptr,unsigned type,unsigned endpoint,unsigned code)
{
    unsigned index=m->produced%256,cycle=1^((m->produced/256)&1);++m->produced;
    memory.event[index]=(struct xhci_trb){(uint32_t)ptr,(uint32_t)(ptr>>32),code<<24,(1u<<24)|(endpoint<<16)|(type<<10)|cycle};
}
static bool read_reg(void *ctx,uint32_t offset,uint32_t *value)
{ struct model *m=ctx;assert(offset==0x40 || offset==0x44);*value=offset==0x40 ? 1 : m->stopped ? 1 : 0;return true; }
static bool write_reg(void *ctx,uint32_t offset,uint32_t value)
{
    struct model *m=ctx;
    if (offset==0x40) { assert(!value);m->stopped=true;return true; }
    if (offset==0x2038 || offset==0x203c) { return true; }
    if (offset==0x1000) {
        struct xhci_trb *c=&memory.command[m->next_command++];unsigned type=(c->control>>10)&63;
        assert(c->control==((type<<10)|(1u<<24)|(m->endpoint<<16)|1));
        if (type==14) { assert(m->step==0 && !c->parameter_lo && !c->parameter_hi);m->step=1; }
        else {
            assert(type==16 && m->step==2);m->step=3;
            struct pipe *p=m->endpoint==3 ? &disk.in : &disk.out;
            uint64_t ptr=(uint64_t)c->parameter_hi<<32|c->parameter_lo;
            assert(ptr==((p->dma+p->index*16)|p->cycle));
        }
        emit(m,address(c),33,0,m->mode==1 && type==14 ? 19 : 1);return true;
    }
    assert(offset==0x1004);
    if (value==1) {
        struct xhci_trb *setup=&memory.ep0[m->next_ep];m->next_ep+=2;
        assert(m->step==1 && setup->parameter_lo==0x102 && setup->parameter_hi==(m->endpoint==3 ? 0x81u : 2u));
        assert(setup->status==8 && setup->control==((2u<<10)|(1u<<6)|1));
        assert(memory.ep0[m->next_ep-1].control==((4u<<10)|(1u<<5)|(1u<<16)|1));m->step=2;
        emit(m,address(&memory.ep0[m->next_ep-1]),32,1,m->mode==2 ? 6 : 1);return true;
    }
    assert(value==m->endpoint);++m->bulk_calls;struct pipe *p=value==3 ? &disk.in : &disk.out;
    struct xhci_trb *trb=&p->ring[p->index-1];
    if (m->mode==3) { return true; }
    emit(m,address(trb)+(m->mode==4 ? 16 : 0),32,value,m->stall ? 6 : 1);
    return true;
}
static bool map_reg(void *ctx,const void *p,size_t n,uint64_t *out)
{ (void)ctx;(void)n;*out=address(p);return true; }
static void barrier(void *ctx) { (void)ctx; }
static uint64_t ticks(void *ctx) { return ++((struct model *)ctx)->time; }
static void seed(struct model *m,bool input,bool wrap)
{
    memset(&memory,0,sizeof(memory));memset(&disk,0,sizeof(disk));memset(&state,0,sizeof(state));*m=(struct model){.stall=true,.endpoint=input ? 3 : 4};
    disk.online=true;disk.io=(struct xhci_test_io){{read_reg,m,0x5000},write_reg,map_reg,barrier,ticks,100};
    disk.snapshot.ports=3;disk.info.bulk_in=0x81;disk.info.bulk_out=2;
    state.command_dma=address(memory.command);state.event_dma=address((const void *)memory.event);
    disk.t=(struct session){.io=&disk.io,.m=&memory,.s=&state,.snapshot=&disk.snapshot,.event=&disk.events,
        .op=0x40,.rt=0x2000,.db=0x1000,.slot=1,.ep0=address(memory.ep0),.data=address(memory.data)};
    disk.in=(struct pipe){.ring=memory.bulk_in,.dma=address(memory.bulk_in),.endpoint=3,.cycle=1,.index=input && wrap ? 254 : 0};
    disk.out=(struct pipe){.ring=memory.bulk_out,.dma=address(memory.bulk_out),.endpoint=4,.cycle=1,.index=!input && wrap ? 254 : 0};
    disk.cbw=address(memory.cbw);disk.csw=address(memory.csw);
}
int main(void)
{
    for (unsigned input=0;input<2;++input) { for (unsigned wrap=0;wrap<2;++wrap) {
        struct model m;seed(&m,input!=0,wrap!=0);unsigned actual=999;unsigned char bytes[512]={0};
        assert(bulk(NULL,input!=0,bytes,512,&actual)==BLOCK_STALL && actual==0 && disk.online && !m.stopped);
        assert(clear_halt(NULL,input!=0)==BLOCK_OK && m.step==3 && !m.stopped && disk.online);
        struct pipe *p=input ? &disk.in : &disk.out;
        assert(p->index==(wrap ? 0u : 1u) && p->cycle==(wrap ? 0u : 1u) && p->wraps==wrap);
        m.stall=false;assert(bulk(NULL,input!=0,bytes,512,&actual)==BLOCK_OK && actual==512 && m.bulk_calls==2);
        assert(disk.clears==1 && !m.stopped);
    } }
    for (unsigned mode=1;mode<=4;++mode) {
        struct model m;seed(&m,true,false);m.mode=mode;unsigned actual;unsigned char bytes[13];
        enum block_result r=bulk(NULL,true,bytes,13,&actual);
        if (mode<3) { assert(r==BLOCK_STALL);r=clear_halt(NULL,true); }
        assert(r==(mode==3 ? BLOCK_TIMEOUT : BLOCK_IO) && !disk.online && m.stopped);
    }
    struct model m;seed(&m,true,false);disk.clears=32;assert(clear_halt(NULL,true)==BLOCK_IO && !m.step);
    puts("PASS: production xHCI matched STALL stays online; Reset Endpoint -> exact CLEAR_FEATURE endpoint/index -> Set Dequeue with correct cycle/wrap; next transfer succeeds; reset/control/mismatched/timeout failures halt and recovery is bounded");
    return 0;
}
