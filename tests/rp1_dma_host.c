#include "rp1_dma_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { CONFIG=0x10, ENABLE=0x18, STATUS=0x388, CLEAR=0x398, START=0x404, STOP=0x400 };
enum mode { COMPLETE, BUSY, ERROR, TIMEOUT, STUCK, BAD_SETUP, BAD_RESTORE, SIGNAL_STUCK };
struct engine {
    uint32_t registers[0x400/4];
    enum mode mode;
    uint64_t ticks;
    unsigned writes,starts,stops,barriers;
};

static bool read_reg(void *context,unsigned offset,uint32_t *out)
{
    const struct engine *engine=context;
    assert(!(offset&3) && offset<sizeof(engine->registers));
    *out=engine->registers[offset/4];
    return true;
}

static bool write_reg(void *context,unsigned offset,uint32_t value)
{
    struct engine *e=context;
    assert(!(offset&3) && offset<sizeof(e->registers));
    ++e->writes;
    if (offset>=0x300 && !(e->registers[CONFIG/4]&1)) { return true; }
    if (offset==ENABLE) {
        if (value==START) {
            assert(e->barriers && (e->registers[CONFIG/4]&1));
            assert(e->registers[0x320/4]==0x8000000fu && e->registers[0x328/4]==0x2000);
            assert(e->registers[0x32c/4]==0x10 && e->registers[0x390/4]==0x07c08004);
            ++e->starts;
            e->registers[ENABLE/4]=e->mode==COMPLETE || e->mode==BAD_RESTORE ? 0 : 4;
            e->registers[STATUS/4]=(e->mode==COMPLETE || e->mode==BAD_RESTORE) ? 2 :
                                   e->mode==ERROR ? 128 : 0;
            e->registers[0x300/4]=42; /* Channel state loaded from the descriptor. */
        } else {
            assert(value==STOP);
            ++e->stops;
            if (e->mode!=STUCK) { e->registers[ENABLE/4]=0; }
        }
        return true;
    }
    if (offset==CLEAR) { e->registers[STATUS/4]&=~value; return true; }
    if (offset==0x328 && value==0x2000 && e->mode==BAD_SETUP) { return false; }
    if (offset==0x300 && e->mode==BAD_RESTORE) { return false; }
    if (offset==0x320) { value|=0x80000000u; } /* Fixed bit outside the transfer fields. */
    if (offset==0x390) {
        assert(!(value&0x07c08004));
        value|=0x07c08004; /* The physical Pi 5 readback after writing zero. */
        if (e->mode==SIGNAL_STUCK) { value|=2; }
    }
    e->registers[offset/4]=value;
    return true;
}

static uint64_t now(void *context) { return ((struct engine *)context)->ticks++; }
static void barrier(void *context) { ++((struct engine *)context)->barriers; }

static void verify(enum mode mode,enum rp1_dma_result expected,unsigned starts,unsigned stops,uint32_t completion)
{
    struct engine engine={.mode=mode};
    engine.registers[0x300/4]=0x1000;
    engine.registers[0x320/4]=0x80000000u;
    engine.registers[0x390/4]=0x07c08004 | (mode==SIGNAL_STUCK ? 2 : 0);
    if (mode==BUSY) { engine.registers[ENABLE/4]=8; }
    struct engine original=engine;
    const struct rp1_dma_io io={&engine,read_reg,write_reg,now,barrier};
    struct rp1_dma_report report;
    assert(rp1_dma_copy(&io,0x1000002001ULL,5,&report)==RP1_DMA_IO && !engine.writes);
    assert(rp1_dma_copy(&io,0x1000002000ULL,5,&report)==expected);
    assert(engine.starts==starts && engine.stops==stops && report.status==completion);
    assert(report.cleanup_failed==(expected==RP1_DMA_RETAIN));
    if (mode==BAD_SETUP) {
        assert(report.register_failed && report.offset==0x328 && report.expected==0x2000 && !report.observed);
    }
    if (mode==BAD_RESTORE) {
        assert(report.register_failed && report.offset==0x300 && report.expected==0x1000 && report.observed==42);
    }
    if (mode==SIGNAL_STUCK) {
        assert(report.register_failed && report.offset==0x390 &&
               report.expected==0 && report.observed==0x07c08006);
    }
    if (expected!=RP1_DMA_RETAIN) {
        assert(!memcmp(engine.registers,original.registers,sizeof(engine.registers)));
    }
    if (mode==BUSY) { assert(!engine.writes); }
    if (mode==STUCK) { assert(engine.registers[ENABLE/4]==4 && engine.registers[0x328/4]==0x2000); }
}

int main(void)
{
    struct rp1_dma_descriptor descriptor;
    rp1_dma_descriptor_init(&descriptor,0x1000002040ULL,0x10000020c0ULL);
    assert(descriptor.source==0x1000002040ULL && descriptor.destination==0x10000020c0ULL);
    assert(descriptor.blocks==15 && !descriptor.next && descriptor.control_low==0x1200 &&
           descriptor.control_high==0xc0008040 && !descriptor.unused[5]);
    verify(COMPLETE,RP1_DMA_OK,1,0,2);
    verify(BUSY,RP1_DMA_BUSY,0,0,0);
    verify(ERROR,RP1_DMA_ERROR,1,1,128);
    verify(TIMEOUT,RP1_DMA_TIMEOUT,1,1,0);
    verify(STUCK,RP1_DMA_RETAIN,1,1,0);
    verify(BAD_SETUP,RP1_DMA_IO,0,0,0);
    verify(BAD_RESTORE,RP1_DMA_RETAIN,1,0,2);
    verify(SIGNAL_STUCK,RP1_DMA_IO,0,0,0);
    puts("PASS: RP1 DMA gated setup, reserved bits, fault readbacks, timeout and register restoration.");
    return 0;
}
