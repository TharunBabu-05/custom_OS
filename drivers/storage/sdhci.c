/* Firmware-handoff SDHC/SDXC read candidate. Preserve card selection, bus width,
 * clocks, tuning and voltage. Only CMD17 single-block reads; no card writes,
 * reset/reinitialization, DMA, voltage switch or speculative RCA probing. */
#include "sdhci.h"
enum { BLOCK_SIZE_COUNT=4, ARGUMENT=8, COMMAND=12, RESPONSE=16, DATA=32,
    PRESENT=36, CONTROL0=40, CONTROL1=44, INTERRUPT=48, STATUS_ENABLE=52,
    SIGNAL_ENABLE=56, CQ_CONFIG=0x208 };
#define ERRORS 0xffff8000u
#define READ_STATUS (ERRORS|0x23u)
static uint32_t rd(struct sdhci_host *h,unsigned at) { return h->io.read(h->io.context,at); }
static void wr(struct sdhci_host *h,unsigned at,uint32_t value) { h->io.write(h->io.context,at,value); }
static enum block_result wait_irq(struct sdhci_host *h,uint32_t wanted)
{
    uint64_t start=h->io.ticks(h->io.context);
    for (unsigned spins=0;spins<10000000;++spins) {
        uint32_t value=rd(h,INTERRUPT); h->last_interrupt=value;
        if (value&ERRORS) { return (value&((1u<<16)|(1u<<20))) ? BLOCK_TIMEOUT : BLOCK_IO; }
        if (value&wanted) { wr(h,INTERRUPT,wanted); return BLOCK_OK; }
        if (h->io.ticks(h->io.context)-start>=h->io.hz) { break; }
    }
    return BLOCK_TIMEOUT;
}
enum block_result sdhci_read_sector(void *context,uint64_t lba,unsigned char *data)
{
    struct sdhci_host *h=context;
    if (!h || !h->ready) { return BLOCK_NOT_READY; }
    if (!data || lba>UINT32_MAX) { return BLOCK_ARGUMENT; }
    uint64_t start=h->io.ticks(h->io.context);
    for (unsigned spins=0;;++spins) {
        if (!(rd(h,PRESENT)&3)) { break; }
        if (spins==1000000 || h->io.ticks(h->io.context)-start>=h->io.hz) {
            h->ready=false; h->error=BLOCK_TIMEOUT; return h->error;
        }
    }
    wr(h,INTERRUPT,READ_STATUS);
    wr(h,BLOCK_SIZE_COUNT,(1u<<16)|512u);
    wr(h,ARGUMENT,(uint32_t)lba);
    wr(h,COMMAND,(17u<<24)|(0x3au<<16)|0x10u); /* R1, CRC/index, data, read; DMA off */
    enum block_result result=wait_irq(h,1);
    if (result==BLOCK_OK) {
        h->last_response=rd(h,RESPONSE);
        if (h->last_response&0xfdffe008u) { result=BLOCK_IO; }
    }
    if (result==BLOCK_OK) { result=wait_irq(h,1u<<5); }
    if (result==BLOCK_OK) {
        for (unsigned i=0;i<512;i+=4) {
            uint32_t value=rd(h,DATA);
            for (unsigned j=0;j<4;++j) { data[i+j]=(unsigned char)(value>>(j*8)); }
        }
        result=wait_irq(h,2);
    }
    h->error=result;
    if (result!=BLOCK_OK) { h->ready=false; }
    return result;
}
bool sdhci_attach(struct sdhci_host *h,const struct sdhci_io *io,struct block_device *device)
{
    if (!h || !io || !device || !io->read || !io->write || !io->ticks || !io->hz || io->hz>10000000000ULL) { return false; }
    *h=(struct sdhci_host){.io=*io,.error=BLOCK_UNSUPPORTED};
    uint32_t state=rd(h,PRESENT),clock=rd(h,CONTROL1),control=rd(h,CONTROL0);
    /* Require an idle, powered, clocked, non-CQE SD host handed over by firmware.
     * An SDHC/SDXC card must still be selected in transfer state. The first
     * sector read verifies that premise. Card capacity is not inferred here. */
    if (!(state&(1u<<16)) || (state&3) || (clock&7)!=7 || !(control&(1u<<8)) ||
        (clock&0x07000000) || (rd(h,CQ_CONFIG)&1)) { return false; }
    wr(h,SIGNAL_ENABLE,0); wr(h,STATUS_ENABLE,READ_STATUS);
    wr(h,CONTROL1,(clock&~0x000f0000u)|(14u<<16));
    h->ready=true;
    unsigned char first[512];
    if (sdhci_read_sector(h,0,first)!=BLOCK_OK) { return false; }
    if (first[510]!=0x55 || first[511]!=0xaa) { h->error=BLOCK_UNSUPPORTED; h->ready=false; return false; }
    *device=(struct block_device){.name="pi5-sdhci-handoff",.sectors=1ULL<<32,.capacity_known=false,.read=sdhci_read_sector,.context=h};
    return true;
}
enum block_result sdio_query(const struct sdhci_io *io,uint32_t *r4)
{
    if (!io || !r4 || !io->read || !io->write || !io->ticks || !io->hz || io->hz>10000000000ULL) { return BLOCK_ARGUMENT; }
    struct sdhci_host host={.io=*io};
    if ((rd(&host,CONTROL1)&7)!=7 || (rd(&host,PRESENT)&3) || (rd(&host,CQ_CONFIG)&1)) { return BLOCK_NOT_READY; }
    wr(&host,SIGNAL_ENABLE,0); wr(&host,STATUS_ENABLE,ERRORS|1u); wr(&host,INTERRUPT,ERRORS|1u);
    wr(&host,ARGUMENT,0); wr(&host,COMMAND,(5u<<24)|(2u<<16)); /* R4 has no CRC/index check. */
    enum block_result r=wait_irq(&host,1);
    if (r==BLOCK_OK) { *r4=rd(&host,RESPONSE); }
    return r;
}
