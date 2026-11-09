#include <stdio.h>
#include <string.h>
#include "brcm_start.h"
#include "host_assert.h"
struct model {
    uint32_t control,reset,vector,d11_control,d11_reset;
    unsigned mode,calls,fail_at,writes,clock_reads,shared_reads;
    bool cleared,released;
};
static struct model m;
static struct brcm_fw_state fw;
static struct brcm_chip_info cores;
static enum block_result access(void)
{ ++m.calls;return m.fail_at==m.calls ? BLOCK_IO : BLOCK_OK; }
static void put32(void *buffer,uint32_t v)
{ unsigned char *p=buffer;for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static enum block_result read32(void *ctx,uint32_t at,uint32_t *v)
{
    (void)ctx;if (access()!=BLOCK_OK) { return BLOCK_IO; }
    if (at==0x18102408) { *v=m.mode==13 && m.released && m.control==1 ? 0x21 : m.control; }
    else if (at==0x18102800) { *v=m.reset; }
    else if (at==0x18101800) { *v=m.d11_reset; }
    else if (at==0x18101408) { *v=m.d11_control; }
    else if (at==0x1800404c) { *v=m.mode==9 ? 0x40010 : m.mode==10 ? 0x5000a : m.mode==21 ? 0 : 0x4000a; }
    else { assert(0); }
    if (m.mode==18) { *v=UINT32_MAX; }return BLOCK_OK;
}
static enum block_result write32(void *ctx,uint32_t at,uint32_t v)
{
    (void)ctx;if (access()!=BLOCK_OK) { return BLOCK_IO; }++m.writes;
    if (at==0x18004020) { assert(v==UINT32_MAX && !m.vector && !m.released);m.cleared=true; }
    else if (at==0x18101800) { assert(v==0 && m.control==0x21 && m.vector);if (m.mode!=22) { m.d11_reset=0; } }
    else if (at==0x18101408) { assert(v==5 && !m.d11_reset && m.control==0x21);m.d11_control=v; }
    else if (at==0x18102408) {
        assert(m.vector==0xb83ef198 && m.cleared);
        if (v==0x23) { assert(m.control==0x21 && !m.reset && !m.d11_reset && m.d11_control==5); }
        else if (v==3) { assert(m.control==0x23 && m.reset==1); }
        else { assert(v==1 && m.control==3 && !m.reset && m.released); }
        m.control=v;
    } else {
        assert(at==0x18102800 && (v==0 || v==1));
        if (v) { assert(m.control==0x23);if (m.mode!=11) { m.reset=1; } }
        else { assert(m.control==3 && m.reset==1);if (m.mode!=12) { m.reset=0;m.released=true; } }
    }
    return BLOCK_OK;
}
static enum block_result ram(void *ctx,uint32_t at,void *p,size_t n)
{
    (void)ctx;if (access()!=BLOCK_OK) { return BLOCK_IO; }memset(p,0,n);
    if (at==0x198000) { assert(n==4 && !m.released);put32(p,fw.reset_vector^(m.mode==19)); }
    else if (at==0x25fffc) {
        assert(n==4);uint32_t token=~436u<<16|436u;
        if (!m.released || m.mode==2 || m.shared_reads++<2) { put32(p,token^(m.mode==20)); }
        else { put32(p,m.mode==3 ? 0x18004000 : m.mode==4 ? 0x220002 : m.mode==14 ? 0 : m.mode==15 ? 0xedcb1234 : m.mode==17 ? UINT32_MAX : 0x220000); }
    } else {
        assert(at==0x220000 && n==64 && m.released);
        put32(p,m.mode==5 ? 4 : m.mode==6 ? 0x203 : m.mode==7 ? 0x403 : m.mode==16 ? 0 : 3);
        put32((unsigned char *)p+4,0x221000);put32((unsigned char *)p+16,42);
        put32((unsigned char *)p+20,m.mode==8 ? 0x260000 : 0x230000);
    }return BLOCK_OK;
}
static enum block_result vector(void *ctx,uint32_t v)
{
    (void)ctx;if (access()!=BLOCK_OK) { return BLOCK_IO; }
    assert(m.cleared && m.control==0x21 && !m.reset && v==0xb83ef198);m.vector=v;++m.writes;return BLOCK_OK;
}
static enum block_result clock_io(void *ctx,bool write,uint8_t *v)
{
    (void)ctx;if (access()!=BLOCK_OK) { return BLOCK_IO; }assert(m.released && m.control==1);
    if (write) { assert(*v==0x10);++m.writes; }
    else { *v=m.mode==23 ? 0x90 : m.mode==1 || m.clock_reads++<2 ? 0x50 : 0xd0; }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ (void)ctx;assert(us==10 || us==50 || us==5000 || us==10000);return access(); }
static void seed(void)
{
    memset(&m,0,sizeof(m));m.control=0x21;m.d11_control=7;m.d11_reset=1;
    fw=(struct brcm_fw_state){.phase=FW_VERIFIED,.error=BLOCK_OK,.files_valid=true,.attempted=true,.complete=true,.cpu_halted=true,.d11_disabled=true,.uploaded=609309,.verified=609309,.reset_vector=0xb83ef198,.nvram_size=1748,.nvram_address=0x25f92c};
    put32(fw.nvram+1744,~436u<<16|436u);
    cores=(struct brcm_chip_info){.error=BLOCK_OK,.cores_valid=true,.ram_valid=true,.bank_restored=true,.core_count=5,.banks=8,.ram_base=0x198000,.ram_bytes=819200};
    const uint16_t id[]={0x800,0x812,0x83e,0x83c,0x829};const uint8_t rev[]={51,54,9,14,21};
    for (unsigned i=0;i<5;++i) { cores.core[i]=(struct brcm_core){.id=id[i],.revision=rev[i],.base=0x18000000+i*4096,.wrapper=0x18100000+i*4096}; }
}
int main(void)
{
    struct brcm_start_io io={NULL,read32,write32,ram,vector,clock_io,delay};struct brcm_start_state s={0};
    seed();assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_OK);
    assert(s.ready && s.may_run && s.phase==START_READY && s.shared==0x220000 && s.flags==3 && s.console==0x230000 && s.mailbox==0x4000a);
    assert(s.polls==3 && !fw.cpu_halted && !fw.d11_disabled && m.released && m.reset==0 && m.control==1);
    unsigned calls=m.calls,total=calls;
    assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_OK && m.calls==calls);
    for (unsigned mode=1;mode<=20;++mode) {
        seed();m.mode=mode;s=(struct brcm_start_state){0};
        enum block_result r=brcm_fw_start(&io,0x15264345,&cores,&fw,&s);
        assert(r!=BLOCK_OK && !s.ready);
        if (mode==1 || mode==2 || mode==11 || mode==12 || mode==14) { assert(r==BLOCK_TIMEOUT); }
        if (mode==3 || mode==4 || mode==8 || mode==15 || mode==17) { assert(r==BLOCK_RANGE); }
        if (mode==5 || mode==10 || mode==16) { assert(r==BLOCK_UNSUPPORTED); }
        assert(s.may_run==(mode!=11 && mode!=18 && mode!=19 && mode!=20));
        calls=m.calls;
        if (s.attempted) { assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==r && m.calls==calls); }
        if (s.may_run) { assert(!fw.cpu_halted && !fw.d11_disabled); }
    }
    seed();m.mode=21;s=(struct brcm_start_state){0};
    assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_OK && s.ready && !s.mailbox);
    seed();m.mode=22;s=(struct brcm_start_state){0};
    assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_TIMEOUT && !s.ready && !s.may_run && fw.cpu_halted && !fw.d11_disabled && m.d11_reset);
    seed();m.mode=23;s=(struct brcm_start_state){0};
    assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_TIMEOUT && !s.ready && s.may_run && s.phase==START_CLOCK);
    /* A failure at every operation of the successful sequence must stop at
     * that operation, preserve diagnostics and never issue cleanup writes. */
    for (unsigned fail=1;fail<=total;++fail) {
        seed();m.fail_at=fail;s=(struct brcm_start_state){0};
        assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_IO && !s.ready && m.calls==fail);
        calls=m.calls;if (s.attempted) { assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_IO && m.calls==calls); }
        if (s.may_run) { assert(!fw.cpu_halted && !fw.d11_disabled); }
    }
    for (unsigned bad=0;bad<11;++bad) {
        seed();s=(struct brcm_start_state){0};
        if (bad==0) { fw.complete=false; }if (bad==1) { fw.cpu_halted=false; }
        if (bad==2) { fw.d11_disabled=false; }if (bad==3) { fw.files_valid=false; }
        if (bad==4) { fw.nvram_size=4097; }if (bad==5) { cores.core[2].revision=8; }
        if (bad==6) { cores.bank_restored=false; }if (bad==7) { fw.verified=0; }
        if (bad==8) { fw.nvram_address=0; }if (bad==9) { fw.phase=FW_CODE; }
        if (bad==10) { fw.error=BLOCK_IO; }
        assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_NOT_READY && !m.calls && !s.attempted);
    }
    seed();s=(struct brcm_start_state){0};m.control=1;
    assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_NOT_READY && !m.writes);
    seed();s=(struct brcm_start_state){0};
    fw.nvram[1744]^=1;assert(brcm_fw_start(&io,0x15264345,&cores,&fw,&s)==BLOCK_IO && !m.writes);
    assert(brcm_fw_start(NULL,0,&cores,&fw,&s)==BLOCK_ARGUMENT);
    printf("PASS: D11 reset completion before CR4 release, vector/readback, HT/shared memory; %u injected I/O failures, 22 negative boot states, optional pre-F2 mailbox and one-shot refusal\n",total);
    puts("PASS: unverified upload/profile/live-state refusal, shared pointer bounds/version/assert/trap checks, and honest CPU state after release failure");
    return 0;
}
