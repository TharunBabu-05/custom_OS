#include "rp1_dma_test.h"

enum {
    GLOBAL_CONFIG=0x10, CHANNEL_ENABLE=0x18, GLOBAL_STATUS=0x30, RESET=0x58,
    CHANNEL_BASE=0x100*(1+RP1_DMA_TEST_CHANNEL),
    CHANNEL_CONFIG=CHANNEL_BASE+0x20, DESCRIPTOR=CHANNEL_BASE+0x28,
    STATUS_ENABLE=CHANNEL_BASE+0x80, STATUS=CHANNEL_BASE+0x88,
    SIGNAL_ENABLE=CHANNEL_BASE+0x90, CLEAR_STATUS=CHANNEL_BASE+0x98,
    CHANNEL_BIT=1u<<RP1_DMA_TEST_CHANNEL, WRITE_ENABLE=CHANNEL_BIT<<8,
    ENGINE_ENABLE=1, INTERRUPT_ENABLE=2, DONE=2, ERRORS=0x3f7fe0,
    /* Documented interrupt event bits. RP1 reads reserved bits 2, 15
     * and 22..26 as ones in the channel signal-enable register. */
    SIGNAL_BITS=0xf83f7ffbU
};

_Static_assert(sizeof(struct rp1_dma_descriptor)==64,"RP1 DMA descriptor layout");

void rp1_dma_descriptor_init(struct rp1_dma_descriptor *descriptor,uint64_t source,uint64_t destination)
{
    *descriptor=(struct rp1_dma_descriptor){
        .source=source,.destination=destination,.blocks=RP1_DMA_TEST_BYTES/4-1,
        .control_low=(2u<<11)|(2u<<8), /* 32-bit transfers on AXI master 0. */
        .control_high=(3u<<30)|(1u<<15)|(1u<<6) /* Last, valid, one beat per burst. */
    };
}

static bool read_register(const struct rp1_dma_io *io,unsigned reg,uint32_t *out)
{ return io->read(io->context,reg,out) && *out!=UINT32_MAX; }

static bool fault(struct rp1_dma_report *report,unsigned reg,uint32_t expected,uint32_t observed)
{
    if (!report->register_failed) {
        report->register_failed=true;
        report->offset=reg;
        report->expected=expected;
        report->observed=observed;
    }
    return false;
}

static bool set_register(const struct rp1_dma_io *io,unsigned reg,uint32_t value,uint32_t mask,
                         struct rp1_dma_report *report)
{
    uint32_t current=UINT32_MAX;
    bool written=io->write(io->context,reg,value);
    bool read=io->read(io->context,reg,&current);
    return (written && read && ((current^value)&mask)==0) || fault(report,reg,value,current);
}

static bool wait_disabled(const struct rp1_dma_io *io,uint64_t timeout)
{
    uint64_t start=io->ticks(io->context);
    for (unsigned i=0;i<1000000 && io->ticks(io->context)-start<timeout;++i) {
        uint32_t enabled;
        if (!read_register(io,CHANNEL_ENABLE,&enabled)) { return false; }
        if (!(enabled&CHANNEL_BIT)) { return true; }
    }
    return false;
}

static enum rp1_dma_result wait_complete(const struct rp1_dma_io *io,uint64_t timeout,struct rp1_dma_report *report)
{
    uint64_t start=io->ticks(io->context);
    for (unsigned i=0;i<1000000 && io->ticks(io->context)-start<timeout;++i) {
        uint32_t enabled;
        if (!read_register(io,STATUS,&report->status)) {
            fault(report,STATUS,DONE,UINT32_MAX); return RP1_DMA_IO;
        }
        if (!read_register(io,CHANNEL_ENABLE,&enabled)) {
            fault(report,CHANNEL_ENABLE,0,UINT32_MAX); return RP1_DMA_IO;
        }
        if (report->status&ERRORS) { return RP1_DMA_ERROR; }
        if ((report->status&DONE) && !(enabled&CHANNEL_BIT)) { return RP1_DMA_OK; }
    }
    return RP1_DMA_TIMEOUT;
}

enum rp1_dma_result rp1_dma_copy(const struct rp1_dma_io *io,uint64_t descriptor,uint64_t timeout,struct rp1_dma_report *report)
{
    uint32_t cfg,enabled,pending,reset;
    if (!report) { return RP1_DMA_IO; }
    *report=(struct rp1_dma_report){0};
    if (!io || !io->read || !io->write || !io->ticks || !io->barrier ||
        !timeout || !descriptor || (descriptor&63) || descriptor>=(1ULL<<39) ||
        !read_register(io,GLOBAL_CONFIG,&cfg) || !read_register(io,CHANNEL_ENABLE,&enabled) ||
        !read_register(io,GLOBAL_STATUS,&pending) || !read_register(io,RESET,&reset)) { return RP1_DMA_IO; }
    if ((cfg&INTERRUPT_ENABLE) || (enabled&0xff) || pending || (reset&1)) { return RP1_DMA_BUSY; }
    if (!read_register(io,STATUS,&pending)) { return RP1_DMA_IO; }
    if (pending) { return RP1_DMA_BUSY; }

    /* The engine can load the channel's shadow registers from the LLI.
     * Save those registers as well as the fields explicitly programmed. */
    static const uint16_t saved_regs[]={
        CHANNEL_BASE,CHANNEL_BASE+4,CHANNEL_BASE+8,CHANNEL_BASE+12,
        CHANNEL_BASE+16,CHANNEL_BASE+20,CHANNEL_BASE+24,CHANNEL_BASE+28,
        CHANNEL_CONFIG,CHANNEL_CONFIG+4,DESCRIPTOR,DESCRIPTOR+4,STATUS_ENABLE,SIGNAL_ENABLE
    };
    uint32_t saved[sizeof(saved_regs)/sizeof(*saved_regs)];
    for (unsigned i=0;i<sizeof(saved)/sizeof(*saved);++i) {
        if (!io->read(io->context,saved_regs[i],&saved[i])) { return RP1_DMA_IO; }
    }

    enum rp1_dma_result result=RP1_DMA_IO;
    /* Open the global engine first, as in the DesignWare driver. Global
     * interrupt generation stays off and CH_EN is armed only after readback. */
    if (set_register(io,GLOBAL_CONFIG,cfg|ENGINE_ENABLE,UINT32_MAX,report) &&
        set_register(io,SIGNAL_ENABLE,0,SIGNAL_BITS,report) &&
        set_register(io,STATUS_ENABLE,DONE|ERRORS,DONE,report) &&
        set_register(io,CHANNEL_CONFIG,15,15,report) && /* Linked-list source and destination. */
        set_register(io,CHANNEL_CONFIG+4,RP1_DMA_TEST_CHANNEL<<17,7|(3u<<3),report) &&
        set_register(io,DESCRIPTOR,(uint32_t)descriptor,UINT32_MAX,report) &&
        set_register(io,DESCRIPTOR+4,(uint32_t)(descriptor>>32),UINT32_MAX,report)) {
        io->barrier(io->context);
        if (io->write(io->context,CHANNEL_ENABLE,WRITE_ENABLE|CHANNEL_BIT)) {
            result=wait_complete(io,timeout,report);
        } else {
            fault(report,CHANNEL_ENABLE,WRITE_ENABLE|CHANNEL_BIT,UINT32_MAX);
        }
        if (result!=RP1_DMA_OK && (!io->write(io->context,CHANNEL_ENABLE,WRITE_ENABLE) ||
                                 !wait_disabled(io,timeout))) {
            report->cleanup_failed=true;
            return RP1_DMA_RETAIN;
        }
    }
    io->barrier(io->context);
    uint32_t value;
    bool clean=read_register(io,STATUS,&value);
    if (clean && (value&SIGNAL_BITS)) { clean=io->write(io->context,CLEAR_STATUS,value&SIGNAL_BITS); }
    for (unsigned i=0;i<sizeof(saved)/sizeof(*saved);++i) {
        unsigned reg=saved_regs[i];
        uint32_t mask=reg==SIGNAL_ENABLE || reg==STATUS_ENABLE ? SIGNAL_BITS : UINT32_MAX;
        clean &= set_register(io,reg,saved[i]&mask,mask,report);
    }
    clean &= set_register(io,GLOBAL_CONFIG,cfg,UINT32_MAX,report);
    if (!clean || !read_register(io,STATUS,&value) || value) {
        report->cleanup_failed=true;
        return RP1_DMA_RETAIN;
    }
    return result;
}
