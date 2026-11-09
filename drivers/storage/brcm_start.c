/* Register/protocol definitions checked against brcmfmac chip.c and sdio.c.
 * Original bounded startup engine; vendor firmware runs on its own CR4. */
#include "brcm_start.h"
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static enum block_result read_reg(const struct brcm_start_io *io,struct brcm_start_state *s,uint32_t at,uint32_t *v)
{
    s->last_address=at;enum block_result r=io->read32(io->context,at,v);
    if (r==BLOCK_OK) { s->last_word=*v;if (*v==UINT32_MAX) { return BLOCK_IO; } }return r;
}
static enum block_result write_reg(const struct brcm_start_io *io,struct brcm_start_state *s,uint32_t at,uint32_t v)
{ s->last_address=at;return io->write32(io->context,at,v); }
static enum block_result read_ram(const struct brcm_start_io *io,struct brcm_start_state *s,uint32_t at,unsigned char *p,size_t n)
{ s->last_address=at;enum block_result r=io->read_ram(io->context,at,p,n);if (r==BLOCK_OK && n>=4) { s->last_word=le32(p); }return r; }
static bool in_ram(uint32_t at,uint32_t n)
{ return !(at&3) && at>=0x198000 && at<0x25fffc && n<=0x25fffc-at; }
/* All failures preserve the last operation and phase. No cleanup writes after
 * CPU release: firmware owns RAM and may be using the NVRAM token location. */
#define TRY(call) do { r=(call);if (r!=BLOCK_OK) { goto fail; } } while (0)
#define REQUIRE(condition,err) do { if (!(condition)) { r=(err);goto fail; } } while (0)
enum block_result brcm_fw_start(const struct brcm_start_io *io,uint32_t chip,
    const struct brcm_chip_info *cores,struct brcm_fw_state *fw,struct brcm_start_state *s)
{
    if (!io || !fw || !s || !io->read32 || !io->write32 || !io->read_ram || !io->vector || !io->clock || !io->delay_us) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }
    *s=(struct brcm_start_state){.phase=START_CHECK,.error=BLOCK_NOT_READY};
    if (!brcm_fw_profile(chip,cores) || !fw->complete || !fw->files_valid || fw->phase!=FW_VERIFIED || fw->error!=BLOCK_OK || !fw->cpu_halted || !fw->d11_disabled || fw->verified!=609309 || fw->uploaded!=609309 || fw->nvram_size<4 || fw->nvram_size>4096 || fw->nvram_address!=0x260000-fw->nvram_size) { return s->error; }
    enum block_result r;uint32_t v;unsigned char data[64];
    TRY(read_reg(io,s,0x18102408,&v));if ((v&0x23)!=0x21) { fw->cpu_halted=false;REQUIRE(false,BLOCK_NOT_READY); }
    TRY(read_reg(io,s,0x18102800,&v));REQUIRE(v==0,BLOCK_NOT_READY);
    TRY(read_reg(io,s,0x18101800,&v));if (v!=1) { fw->d11_disabled=false;REQUIRE(false,BLOCK_NOT_READY); }
    TRY(read_ram(io,s,0x198000,data,4));s->reset_vector=le32(data);
    REQUIRE(s->reset_vector==fw->reset_vector && s->reset_vector && s->reset_vector!=UINT32_MAX,BLOCK_IO);
    s->token=le32(fw->nvram+fw->nvram_size-4);
    uint32_t words=(fw->nvram_size-4)/4;
    REQUIRE(!(fw->nvram_size&3) && s->token==((~words<<16)|(words&65535)),BLOCK_IO);
    TRY(read_ram(io,s,0x25fffc,data,4));REQUIRE(le32(data)==s->token,BLOCK_IO);
    s->attempted=true;s->phase=START_VECTOR;
    TRY(write_reg(io,s,0x18004020,UINT32_MAX)); /* W1C old SDIO interrupts. */
    s->last_address=0;TRY(io->vector(io->context,s->reset_vector));
    s->phase=START_RESET;
    /* Upload intentionally stopped midway through the D11 AI reset. Finish
     * it with PHY clock enabled, as in the reference passive sequence,
     * before allowing CR4 firmware to access that core. */
    TRY(read_reg(io,s,0x18101408,&v));REQUIRE((v&15)==7,BLOCK_NOT_READY);
    fw->d11_disabled=false;bool d11_reset=true;
    for (unsigned i=0;i<50;++i) {
        TRY(write_reg(io,s,0x18101800,0));TRY(io->delay_us(io->context,50));
        TRY(read_reg(io,s,0x18101800,&v));if (v==0) { d11_reset=false;break; }
    }
    REQUIRE(!d11_reset,BLOCK_TIMEOUT);
    TRY(write_reg(io,s,0x18101408,5));
    TRY(read_reg(io,s,0x18101408,&v));REQUIRE((v&15)==5,BLOCK_IO);
    TRY(write_reg(io,s,0x18102408,0x23));
    TRY(read_reg(io,s,0x18102408,&v));REQUIRE((v&0x23)==0x23,BLOCK_IO);
    TRY(write_reg(io,s,0x18102800,1));TRY(io->delay_us(io->context,10));
    bool reset=false;
    for (unsigned i=0;i<30;++i) {
        TRY(read_reg(io,s,0x18102800,&v));if (v==1) { reset=true;break; }
        TRY(io->delay_us(io->context,10));
    }
    REQUIRE(reset,BLOCK_TIMEOUT);
    s->phase=START_RELEASE;
    /* Clearing CPUHALT is irreversible within this command. Even a failed
     * write/response leaves execution uncertain. Invalidate passive claims. */
    s->may_run=true;fw->cpu_halted=false;fw->d11_disabled=false;
    TRY(write_reg(io,s,0x18102408,3));
    TRY(read_reg(io,s,0x18102408,&v));REQUIRE((v&0x23)==3,BLOCK_IO);
    reset=true;
    for (unsigned i=0;i<50;++i) {
        TRY(write_reg(io,s,0x18102800,0));TRY(io->delay_us(io->context,50));
        TRY(read_reg(io,s,0x18102800,&v));if (v==0) { reset=false;break; }
    }
    REQUIRE(!reset,BLOCK_TIMEOUT);
    TRY(write_reg(io,s,0x18102408,1));
    TRY(read_reg(io,s,0x18102408,&v));REQUIRE((v&0x23)==1,BLOCK_IO);
    s->phase=START_CLOCK;s->last_address=0x1000e;s->clock_csr=0x10;
    /* Replace forced ALP/disabled hardware request with HT request. */
    TRY(io->clock(io->context,true,&s->clock_csr));
    bool ht=false;
    for (unsigned i=0;i<1000;++i) {
        TRY(io->clock(io->context,false,&s->clock_csr));if ((s->clock_csr&0xc0)==0xc0) { ht=true;break; }
        TRY(io->delay_us(io->context,5000));
    }
    REQUIRE(ht,BLOCK_TIMEOUT);
    s->phase=START_SHARED;
    for (unsigned i=0;i<500;++i) {
        s->polls=i+1;TRY(read_ram(io,s,0x25fffc,data,4));s->shared=le32(data);
        if (s->shared!=0 && s->shared!=s->token) { break; }
        TRY(io->delay_us(io->context,10000));
    }
    REQUIRE(s->shared!=0 && s->shared!=s->token,BLOCK_TIMEOUT);
    REQUIRE(in_ram(s->shared,64) && ((~s->shared>>16)&65535)!=(s->shared&65535),BLOCK_RANGE);
    TRY(read_ram(io,s,s->shared,data,sizeof(data)));
    s->flags=le32(data);s->trap=le32(data+4);s->assert_line=le32(data+16);s->console=le32(data+20);
    REQUIRE((s->flags&255)>=1 && (s->flags&255)<=3,BLOCK_UNSUPPORTED);
    REQUIRE(!(s->flags&0x600),BLOCK_IO); /* firmware ASSERT or TRAP */
    REQUIRE(!s->console || in_ram(s->console,16),BLOCK_RANGE);
    TRY(read_reg(io,s,0x1800404c,&s->mailbox));
    REQUIRE(!(s->mailbox&0x10),BLOCK_IO); /* firmware halt */
    if (s->mailbox&0xa) { REQUIRE(((s->mailbox>>16)&255)==4,BLOCK_UNSUPPORTED); }
    /* Shared-memory initialization is the evidence here. Function 2 is still
     * disabled; DEVREADY/FullMAC control replies are a separate milestone. */
    s->ready=true;s->phase=START_READY;s->error=BLOCK_OK;return BLOCK_OK;
fail:
    s->error=r;return r;
}
