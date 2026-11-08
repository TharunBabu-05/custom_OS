/* Polling, one-shot command-ring test. No USB transfer or storage commands. */
#include "xhci_test.h"
static bool rd(const struct xhci_test_io *io,uint32_t offset,uint32_t *value)
{
    if ((offset&3) || offset>io->observer.span || io->observer.span-offset<4) { return false; }
    return io->observer.read32(io->observer.context,offset,value) && *value!=UINT32_MAX;
}
static bool wr(const struct xhci_test_io *io,uint32_t offset,uint32_t value)
{
    if ((offset&3) || offset>io->observer.span || io->observer.span-offset<4) { return false; }
    return io->write32(io->observer.context,offset,value);
}
static bool wr64(const struct xhci_test_io *io,uint32_t offset,uint64_t value)
{ return wr(io,offset,(uint32_t)value) && wr(io,offset+4,(uint32_t)(value>>32)); }
static enum xhci_test_result wait_bits(const struct xhci_test_io *io,uint32_t offset,uint32_t mask,uint32_t wanted)
{
    uint64_t start=io->ticks(io->observer.context);
    for (unsigned spin=0;spin<20000000;++spin) {
        uint32_t value; if (!rd(io,offset,&value)) { return XHCI_TEST_IO; }
        if ((value&mask)==wanted) { return XHCI_TEST_OK; }
        if (io->ticks(io->observer.context)-start>=io->hz*2) { break; }
    }
    return XHCI_TEST_TIMEOUT;
}
static bool map(const struct xhci_test_io *io,const void *p,size_t n,uint64_t alignment,uint64_t *dma)
{
    return !((uintptr_t)p&(alignment-1)) && io->map_dma(io->observer.context,p,n,dma) &&
        !(*dma&(alignment-1)) && n && n-1<=UINT64_MAX-*dma;
}
enum xhci_test_result xhci_ring_test(const struct xhci_test_io *io,struct xhci_test_memory *m,struct xhci_test_state *s)
{
    if (!io || !m || !s || !io->observer.read32 || !io->write32 || !io->map_dma ||
        !io->barrier || !io->ticks || !io->hz || io->hz>UINT64_MAX/2 || ((uintptr_t)m&4095)) { return XHCI_TEST_ARGUMENT; }
    if (s->attempted) { return XHCI_TEST_USED; }
    *s=(struct xhci_test_state){0};
    struct xhci_snapshot snapshot;
    if (xhci_observe(&io->observer,&snapshot)!=USB_PROBE_OK || !snapshot.address64 ||
        !(snapshot.page_sizes&1) || (snapshot.command&2)) { return XHCI_TEST_LAYOUT; }
    unsigned op=snapshot.cap_length; uint32_t hcs2,hcc,db,rt;
    if (!rd(io,8,&hcs2) || !rd(io,16,&hcc) || !rd(io,20,&db) || !rd(io,24,&rt)) { return XHCI_TEST_IO; }
    db&=~3u; rt&=~31u;
    uint32_t limit=io->observer.span<0xc000 ? io->observer.span : 0xc000;
    if (rt>limit-0x40 || db>limit-4 || (rt<op+0x400+(unsigned)snapshot.ports*16) ||
        (db<op+0x400+(unsigned)snapshot.ports*16)) { return XHCI_TEST_LAYOUT; }
    s->scratchpads=((hcs2>>27)&31)|((hcs2>>21)&31)<<5;
    if (s->scratchpads>XHCI_SCRATCHPADS) { return XHCI_TEST_LAYOUT; }
    uint32_t at=(hcc>>16)*4;
    for (unsigned count=0;at && count<64;++count) {
        uint32_t value; if (!rd(io,at,&value)) { return XHCI_TEST_IO; }
        if ((value&255)==1 && (value&(1u<<16))) { return XHCI_TEST_LEGACY; }
        unsigned next=(value>>8)&255; at=next ? at+next*4 : 0;
    }
    uint64_t dcbaa,erst,array,scratch[XHCI_SCRATCHPADS];
    if (!map(io,m->command,sizeof(m->command),4096,&s->command_dma) ||
        !map(io,(const void *)m->event,sizeof(m->event),4096,&s->event_dma) ||
        !map(io,m->dcbaa,sizeof(m->dcbaa),64,&dcbaa) || !map(io,&m->erst,sizeof(m->erst),64,&erst) ||
        !map(io,m->scratch_array,sizeof(m->scratch_array),64,&array)) { return XHCI_TEST_DMA; }
    for (unsigned i=0;i<s->scratchpads;++i) {
        if (!map(io,m->scratch[i],4096,4096,&scratch[i])) { return XHCI_TEST_DMA; }
    }
    enum xhci_test_result r=wait_bits(io,op+4,1u<<11,0);
    if (r!=XHCI_TEST_OK) { return r; }
    s->attempted=true; s->phase=XHCI_HALT;
    if (!wr(io,op,snapshot.command&~13u)) { r=XHCI_TEST_IO; goto stop; }
    r=wait_bits(io,op+4,1,1); if (r!=XHCI_TEST_OK) { goto stop; }
    s->phase=XHCI_RESET;
    if (!wr(io,op,2)) { r=XHCI_TEST_IO; goto stop; }
    r=wait_bits(io,op,2,0); if (r!=XHCI_TEST_OK) { goto stop; }
    r=wait_bits(io,op+4,1u<<11,0); if (r!=XHCI_TEST_OK) { goto stop; }
    /* New memory was never given to firmware; initialize only after quiescing. */
    s->phase=XHCI_SETUP;
    unsigned char *zero=(unsigned char *)m;
    for (size_t i=0;i<sizeof(*m);++i) { zero[i]=0; }
    for (unsigned i=0;i<s->scratchpads;++i) { m->scratch_array[i]=scratch[i]; }
    if (s->scratchpads) { m->dcbaa[0]=array; }
    m->erst.base=s->event_dma; m->erst.size=XHCI_RING_TRBS;
    m->command[XHCI_RING_TRBS-1]=(struct xhci_trb){(uint32_t)s->command_dma,(uint32_t)(s->command_dma>>32),0,(6u<<10)|3};
    for (unsigned i=0;i<4;++i) { m->command[i].control=(23u<<10)|1; }
    io->barrier(io->observer.context);
    if (!wr(io,op+0x38,snapshot.slots<64 ? snapshot.slots : 64) || !wr64(io,op+0x30,dcbaa) ||
        !wr64(io,op+0x18,s->command_dma|1) || !wr(io,rt+0x20,1) || !wr(io,rt+0x24,0) ||
        !wr(io,rt+0x28,1) || !wr64(io,rt+0x30,erst) || !wr64(io,rt+0x38,s->event_dma|8) ||
        !wr(io,op+4,0x1c)) { r=XHCI_TEST_IO; goto stop; }
    s->phase=XHCI_START;
    io->barrier(io->observer.context);
    if (!wr(io,op,1)) { r=XHCI_TEST_IO; goto stop; }
    r=wait_bits(io,op+4,1,0); if (r!=XHCI_TEST_OK) { goto stop; }
    s->phase=XHCI_NOOP;
    /* All four No-ops are queued before ringing; one doorbell starts the batch. */
    if (!wr(io,db,0)) { r=XHCI_TEST_IO; goto stop; }
    uint64_t start=io->ticks(io->observer.context); unsigned consumed=0;
    for (unsigned spin=0;spin<20000000 && s->completed<4;++spin) {
        volatile struct xhci_trb *event=&m->event[consumed%XHCI_RING_TRBS];
        uint32_t control=event->control;
        if ((control&1)==1) {
            io->barrier(io->observer.context);
            uint64_t pointer=(uint64_t)event->parameter_hi<<32|event->parameter_lo;
            uint32_t status=event->status; s->last_pointer=pointer;
            s->last_event_type=(control>>10)&63; s->last_completion=status>>24;
            if (s->last_event_type==33) {
                if (pointer!=s->command_dma+s->completed*16 || s->last_completion!=1) { r=XHCI_TEST_EVENT; goto stop; }
                ++s->completed;
            } else if (s->last_event_type!=34 || !(pointer>>24&255) || (pointer>>24&255)>snapshot.ports) {
                r=XHCI_TEST_EVENT; goto stop;
            }
            if (++consumed>=XHCI_RING_TRBS) { r=XHCI_TEST_EVENT; goto stop; }
            if (!wr64(io,rt+0x38,(s->event_dma+consumed*16)|8)) { r=XHCI_TEST_IO; goto stop; }
        }
        if (!rd(io,op+4,&s->last_status)) { r=XHCI_TEST_IO; goto stop; }
        if (s->last_status&((1u<<2)|(1u<<12))) { r=XHCI_TEST_IO; goto stop; }
        if (io->ticks(io->observer.context)-start>=io->hz*2) { break; }
    }
    r=s->completed==4 ? XHCI_TEST_OK : XHCI_TEST_TIMEOUT;
stop:
    /* Never release or reuse memory, even if a failed controller will not halt. */
    if (r==XHCI_TEST_OK) { s->phase=XHCI_STOP; }
    uint32_t command;
    if (rd(io,op,&command) && !(command&2) && wr(io,op,command&~13u)) {
        enum xhci_test_result stopped=wait_bits(io,op+4,1,1);
        s->halted=stopped==XHCI_TEST_OK;
        if (r==XHCI_TEST_OK && !s->halted) { r=stopped; }
    } else if (r==XHCI_TEST_OK) { r=XHCI_TEST_IO; }
    (void)rd(io,op+4,&s->last_status);
    if (r==XHCI_TEST_OK) { s->phase=XHCI_DONE; }
    return r;
}
const char *xhci_test_error(enum xhci_test_result r)
{
    switch (r) {
    case XHCI_TEST_OK: return "PASS: four xHCI No-op completions matched; controller halted.";
    case XHCI_TEST_ARGUMENT: return "invalid test arguments";
    case XHCI_TEST_LAYOUT: return "unsupported controller layout, page size or scratchpad count";
    case XHCI_TEST_DMA: return "DMA mapping/alignment rejected before controller changes";
    case XHCI_TEST_TIMEOUT: return "controller/event timeout";
    case XHCI_TEST_IO: return "controller register or hardware error";
    case XHCI_TEST_EVENT: return "unexpected event, completion code or command pointer";
    case XHCI_TEST_USED: return "test already attempted; reboot before another attempt";
    default: return "BIOS ownership present; no takeover attempted";
    }
}
