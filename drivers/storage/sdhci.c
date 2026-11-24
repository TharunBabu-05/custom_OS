/* SDHC/SDXC single-sector PIO and a separate legacy-speed diagnostic. */
#include "sdhci.h"
enum { BLOCK_SIZE_COUNT=4, ARGUMENT=8, COMMAND=12, RESPONSE=16, DATA=32,
    PRESENT=36, CONTROL0=40, POWER_CONTROL=41, CONTROL1=44, INTERRUPT=48, STATUS_ENABLE=52,
    SIGNAL_ENABLE=56, CONTROL2=60, CAPABILITIES=64, VERSION=0xfc, CQ_CONFIG=0x208 };
#define ERRORS 0xffff8000u
#define READ_STATUS (ERRORS|0x23u)
#define CARD_INSERTED (1u<<16)
#define CARD_STABLE (1u<<17)
#define CARD_REMOVED (1u<<7)
#define SD_OCR_3V3 0x00300000u
#define CLOCK_CONFIG 0x000fffedu
#define RESET_BITS 0x07000000u
#define BUS_MODE_BITS 0x26u
#define POWER_ENABLE_BITS 0x1100u
static uint32_t rd(struct sdhci_host *h,unsigned at) { return h->io.read(h->io.context,at); }
static void wr(struct sdhci_host *h,unsigned at,uint32_t value) { h->io.write(h->io.context,at,value); }
struct sdhci_card_state sdhci_card_status(const struct sdhci_io *io)
{
    uint32_t state=io->read(io->context,PRESENT);
    return (struct sdhci_card_state){
        .present=!!(state&CARD_INSERTED),.stable=!!(state&CARD_STABLE),.busy=!!(state&3),
        .powered=!!(io->read(io->context,CONTROL0)&(1u<<8)),.clocked=(io->read(io->context,CONTROL1)&7)==7,
        .cqe=!!(io->read(io->context,CQ_CONFIG)&1)
    };
}
static bool card_available(struct sdhci_host *h)
{
    if (h->card_present) { return h->card_present(h->io.context); }
    return (rd(h,PRESENT)&CARD_INSERTED) && !(rd(h,INTERRUPT)&CARD_REMOVED);
}
static enum block_result wait_irq(struct sdhci_host *h,uint32_t wanted)
{
    uint64_t start=h->io.ticks(h->io.context);
    for (unsigned spins=0;spins<10000000;++spins) {
        uint32_t value=rd(h,INTERRUPT); h->last_interrupt=value;
        if (h->card_present ? !card_available(h) : h->ready && (value&CARD_REMOVED)) { return BLOCK_NOT_READY; }
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
        if (!card_available(h)) { h->ready=false; h->error=BLOCK_NOT_READY; return h->error; }
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
    if (result==BLOCK_OK && !card_available(h)) { result=BLOCK_NOT_READY; }
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
    if (!(state&CARD_INSERTED) || (state&3) || (clock&7)!=7 || !(control&(1u<<8)) ||
        (clock&0x07000000) || (rd(h,CQ_CONFIG)&1)) { return false; }
    wr(h,SIGNAL_ENABLE,0); wr(h,STATUS_ENABLE,READ_STATUS|CARD_REMOVED);
    wr(h,INTERRUPT,CARD_REMOVED);
    wr(h,CONTROL1,(clock&~0x000f0000u)|(14u<<16));
    h->ready=true;
    unsigned char first[512];
    if (sdhci_read_sector(h,0,first)!=BLOCK_OK) { return false; }
    if (first[510]!=0x55 || first[511]!=0xaa) { h->error=BLOCK_UNSUPPORTED; h->ready=false; return false; }
    *device=(struct block_device){.name="pi5-sdhci-handoff",.sectors=1ULL<<32,.capacity_known=false,.read=sdhci_read_sector,.context=h};
    return true;
}

static bool wait_register(struct sdhci_host *h,unsigned reg,uint32_t bits,uint32_t expected)
{
    uint64_t start=h->io.ticks(h->io.context);
    for (unsigned spins=0;spins<1000000;++spins) {
        if ((rd(h,reg)&bits)==expected) { return true; }
        if (h->io.ticks(h->io.context)-start>=h->io.hz/10) { break; }
    }
    return false;
}

static bool delay_ms(struct sdhci_host *h,unsigned ms)
{
    uint64_t start=h->io.ticks(h->io.context),duration=(h->io.hz*ms+999)/1000;
    for (unsigned spins=0;spins<1000000;++spins) {
        if (h->io.ticks(h->io.context)-start>=duration) { return true; }
    }
    return false;
}

static enum block_result sd_command(struct sdhci_host *h,struct sdhci_cold_result *out,unsigned cmd,uint32_t arg,unsigned flags)
{
    out->command=cmd;
    if (!card_available(h)) { return BLOCK_NOT_READY; }
    if (!wait_register(h,PRESENT,3,0)) { return BLOCK_TIMEOUT; }
    wr(h,INTERRUPT,READ_STATUS);
    wr(h,ARGUMENT,arg);
    wr(h,COMMAND,(cmd<<24)|(flags<<16));
    enum block_result result=wait_irq(h,1);
    if (result==BLOCK_OK && flags) { h->last_response=rd(h,RESPONSE); }
    return result;
}

static enum block_result identify(struct sdhci_host *h,struct sdhci_cold_result *out)
{
    enum block_result r=sd_command(h,out,0,0,0);
    if (r!=BLOCK_OK) { return r; }
    r=sd_command(h,out,8,0x1aa,0x1a);
    if (r!=BLOCK_OK || (h->last_response&0xfff)!=0x1aa) { return r==BLOCK_OK ? BLOCK_UNSUPPORTED : r; }
    uint64_t start=h->io.ticks(h->io.context);
    do {
        r=sd_command(h,out,55,0,0x1a);
        if (r!=BLOCK_OK) { return r; }
        if (!(h->last_response&32) || (h->last_response&0xfdffe008u)) { return BLOCK_UNSUPPORTED; }
        r=sd_command(h,out,41,(1u<<30)|SD_OCR_3V3,2);
        if (r!=BLOCK_OK) { return r; }
        if (h->last_response&(1u<<31)) { break; }
        if (!delay_ms(h,10)) { return BLOCK_TIMEOUT; }
    } while (h->io.ticks(h->io.context)-start<2*h->io.hz);
    if (!(h->last_response&(1u<<31))) { return BLOCK_TIMEOUT; }
    if (!(h->last_response&(1u<<30)) || !(h->last_response&SD_OCR_3V3) || (h->last_response&(1u<<24))) { return BLOCK_UNSUPPORTED; }
    r=sd_command(h,out,2,0,9);
    if (r!=BLOCK_OK) { return r; }
    r=sd_command(h,out,3,0,0x1a);
    if (r!=BLOCK_OK) { return r; }
    uint32_t rca=h->last_response&0xffff0000u;
    if (!rca || (h->last_response&0xe000)) { return BLOCK_IO; }
    r=sd_command(h,out,9,rca,9);
    if (r!=BLOCK_OK) { return r; }
    /* SDHCI's 136-bit response omits the lowest eight bits. */
    if (((rd(h,RESPONSE+12)>>22)&3)!=1) { return BLOCK_UNSUPPORTED; }
    uint64_t sectors=(((rd(h,RESPONSE+4)>>8)&0x3fffff)+1ULL)*1024;
    r=sd_command(h,out,7,rca,0x1b);
    if (r!=BLOCK_OK) { return r; }
    if (h->last_response&0xffffe008u) { return BLOCK_IO; }
    r=wait_irq(h,2);
    if (r==BLOCK_OK) { out->sectors=sectors; }
    return r;
}

enum block_result sdhci_cold_test(const struct sdhci_io *io,sdhci_write8 write8,bool (*card_present)(void *),
                                  unsigned count,bool tail,struct sdhci_cold_result *out)
{
    if (!io || !out || !count || count>SDHCI_TEST_MAX_SECTORS || !card_present || !write8 || !io->read || !io->write || !io->ticks ||
        io->hz<1000 || io->hz>10000000000ULL) { return BLOCK_ARGUMENT; }
    *out=(struct sdhci_cold_result){0};
    struct sdhci_host h={.io=*io,.card_present=card_present};
    if (!card_available(&h)) { return BLOCK_NOT_READY; }
    uint32_t clock=rd(&h,CONTROL1),control=rd(&h,CONTROL0),status=rd(&h,STATUS_ENABLE),signal=rd(&h,SIGNAL_ENABLE);
    if ((clock&(RESET_BITS|5)) || (control&POWER_ENABLE_BITS) || (rd(&h,PRESENT)&3) || (rd(&h,CQ_CONFIG)&1) ||
        (rd(&h,CONTROL2)&0x800f0000u) || (rd(&h,CAPABILITIES)&0x0100ff00u)!=0x0100c800u ||
        ((rd(&h,VERSION)>>16)&255)!=2) { return BLOCK_UNSUPPORTED; }
    if (!delay_ms(&h,20)) { return BLOCK_TIMEOUT; }
    if (!card_available(&h)) { return BLOCK_NOT_READY; }

    wr(&h,SIGNAL_ENABLE,0);
    wr(&h,STATUS_ENABLE,READ_STATUS);
    uint8_t host_control=(uint8_t)control;
    uint8_t legacy=host_control&~BUS_MODE_BITS;
    if (legacy!=host_control) { write8(io->context,CONTROL0,legacy); }
    write8(io->context,POWER_CONTROL,0x0f);
    uint32_t slow=(14u<<16)|(250u<<8)|1; /* 200 MHz / (2 * 250). */
    wr(&h,CONTROL1,slow);
    enum block_result result=BLOCK_TIMEOUT;
    if ((rd(&h,CONTROL0)&0x1f26u)!=0x0f00) { result=BLOCK_IO; }
    else if (wait_register(&h,CONTROL1,2,2)) {
        wr(&h,CONTROL1,slow|4);
        if (delay_ms(&h,1) && (rd(&h,CONTROL1)&0xffc7)==((slow&0xffc0)|7)) { result=identify(&h,out); }
    }
    if (result==BLOCK_OK && count>out->sectors) { result=BLOCK_RANGE; }
    if (result==BLOCK_OK && tail) { out->first_lba=out->sectors-count; }
    for (unsigned i=0;i<count && result==BLOCK_OK;++i) {
        unsigned char sector[512];
        h.ready=true;
        out->command=17;
        result=sdhci_read_sector(&h,out->first_lba+i,sector);
        if (result==BLOCK_OK) {
            if (!i) { out->hash=2166136261u; }
            for (unsigned j=0;j<sizeof(sector);++j) { out->hash=(out->hash^sector[j])*16777619u; }
            ++out->completed;
        }
    }
    out->last_interrupt=h.last_interrupt;
    out->last_response=h.last_response;
    if (!wait_register(&h,PRESENT,3,0)) {
        wr(&h,CONTROL1,slow|4|(3u<<25));
        if (!wait_register(&h,CONTROL1,RESET_BITS,0) || !wait_register(&h,PRESENT,3,0)) {
            out->restore_errors|=SD_RESTORE_BUSY;
        }
    }
    /* Gate the card clock before restoring the divider and internal clock. */
    uint32_t active_clock=rd(&h,CONTROL1);
    if (active_clock&4) { wr(&h,CONTROL1,active_clock&(CLOCK_CONFIG&~4u)); }
    wr(&h,CONTROL1,clock&CLOCK_CONFIG);
    write8(io->context,POWER_CONTROL,(uint8_t)(control>>8));
    if ((uint8_t)rd(&h,CONTROL0)!=host_control) { write8(io->context,CONTROL0,host_control); }
    wr(&h,INTERRUPT,READ_STATUS);
    wr(&h,STATUS_ENABLE,status);
    wr(&h,SIGNAL_ENABLE,signal);
    /* Clock stable is read-only and may remain set when the clock is disabled. */
    if (!wait_register(&h,CONTROL1,CLOCK_CONFIG|RESET_BITS,clock&CLOCK_CONFIG)) { out->restore_errors|=SD_RESTORE_CLOCK; }
    out->control_before=control;
    out->control_after=rd(&h,CONTROL0);
    /* Voltage-select readback has no operational effect with both buses off. */
    if ((out->control_after^control)&(0xff|POWER_ENABLE_BITS)) { out->restore_errors|=SD_RESTORE_CONTROL; }
    if (rd(&h,STATUS_ENABLE)!=status) { out->restore_errors|=SD_RESTORE_STATUS; }
    if (rd(&h,SIGNAL_ENABLE)!=signal) { out->restore_errors|=SD_RESTORE_SIGNAL; }
    if (rd(&h,PRESENT)&3) { out->restore_errors|=SD_RESTORE_BUSY; }
    return out->restore_errors ? BLOCK_IO : result;
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
