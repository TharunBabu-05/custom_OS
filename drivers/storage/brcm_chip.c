/* Bounded AI EROM discovery and CR4 bank sizing for the observed 4345 rev6.
 * Register format reference: Broadcom brcmfmac chip.c and chipcommon.h. */
#include "brcm_chip.h"
struct reader { const struct brcm_chip_io *io; struct brcm_chip_info *out; unsigned cursor; bool pending; uint32_t peek; };
static enum block_result read_reg(struct reader *r,uint32_t at,uint32_t *value)
{
    r->out->last_address=at;
    enum block_result e=r->io->read32(r->io->context,at,value);
    if (e==BLOCK_OK) { r->out->last_word=*value; if (*value==UINT32_MAX) { return BLOCK_IO; } }
    return e;
}
static enum block_result next(struct reader *r,uint32_t *word)
{
    if (r->pending) { r->pending=false; *word=r->peek; return BLOCK_OK; }
    if (r->cursor>=BRCM_EROM_WORDS || r->out->erom>0x181ffffc-r->cursor*4) { return BLOCK_RANGE; }
    enum block_result e=read_reg(r,r->out->erom+r->cursor*4,word);
    if (e==BLOCK_OK) {
        if (r->cursor<32) { r->out->rom_sample[r->cursor]=*word; }
        ++r->cursor; r->out->rom_words=r->cursor;
    }
    return e;
}
static enum block_result save(struct reader *r,const struct brcm_core *core)
{
    if (!core->base) { return BLOCK_OK; } /* Bridge/port-only component. */
    if (r->out->core_count==BRCM_MAX_CORES) { return BLOCK_RANGE; }
    for (unsigned i=0;i<r->out->core_count;++i) {
        if (r->out->core[i].base==core->base || (core->wrapper && r->out->core[i].wrapper==core->wrapper)) { return BLOCK_UNSUPPORTED; }
    }
    r->out->core[r->out->core_count++]=*core; return BLOCK_OK;
}
static enum block_result components(struct reader *r)
{
    for (;;) {
        uint32_t a,b; enum block_result e=next(r,&a); if (e!=BLOCK_OK) { return e; }
        if (!(a&1)) { continue; }
        if ((a&15)==15) { return BLOCK_OK; }
        if ((a&15)!=1) { return BLOCK_UNSUPPORTED; }
        e=next(r,&b); if (e!=BLOCK_OK) { return e; }
        if ((b&15)!=1) { return BLOCK_UNSUPPORTED; }
        struct brcm_core core={.id=(uint16_t)((a>>8)&0xfff),.revision=(uint8_t)(b>>24)};
        unsigned wraps=((b>>14)&31)+((b>>19)&31),wrapper_type=0;
        for (;;) {
            uint32_t word; e=next(r,&word); if (e!=BLOCK_OK) { return e; }
            if (!(word&1)) { continue; }
            unsigned type=word&15;
            if (type==1 || type==15) { r->pending=true; r->peek=word; break; }
            if (type==3) { if (!wrapper_type) { wrapper_type=3; } continue; }
            if ((type&~8u)!=5) { return BLOCK_UNSUPPORTED; }
            if (!wrapper_type) { wrapper_type=2; }
            bool wide=false;
            if (word&8) { uint32_t high; e=next(r,&high); if (e!=BLOCK_OK) { return e; } wide=high!=0; }
            unsigned size=(word>>4)&3,kind=(word>>6)&3;
            if (size==3) {
                uint32_t size_word; e=next(r,&size_word); if (e!=BLOCK_OK) { return e; }
                /* This is a raw size payload, not a tagged address descriptor.
                 * The Pi reports 0x01000000 here (16 MiB). Only bit 3 says
                 * whether a second size word follows; this region is skipped. */
                if (size_word&8) { uint32_t unused; e=next(r,&unused); if (e!=BLOCK_OK) { return e; } }
            }
            if (wide || size>1) { continue; } /* Only 32-bit 4/8 KiB core regions. */
            uint32_t base=word&0xfffff000;
            if (kind==0 && !core.base) { core.base=base; }
            if (wraps && kind==wrapper_type && !core.wrapper) { core.wrapper=base; }
        }
        /* Match brcmfmac: port-only components are not register cores, except
         * wrapper-less PMU/GCI. Keep their ROM payload properly consumed. */
        if (wraps || core.id==0x827 || core.id==0x840) {
            e=save(r,&core); if (e!=BLOCK_OK) { return e; }
        }
    }
}
static bool core_address(uint32_t at) { return at>=0x18000000 && at<=0x181ff000 && !(at&4095); }
static enum block_result ram(struct reader *r,const struct brcm_core *arm)
{
    struct brcm_chip_info *out=r->out; enum block_result e;
    e=read_reg(r,arm->wrapper+0x408,&out->arm_ioctl); if (e!=BLOCK_OK) { return e; }
    e=read_reg(r,arm->wrapper+0x800,&out->arm_reset); if (e!=BLOCK_OK) { return e; }
    if (out->arm_reset&1) { return BLOCK_NOT_READY; } /* No reset release in this phase. */
    e=read_reg(r,arm->base+4,&out->arm_cap); if (e!=BLOCK_OK) { return e; }
    out->banks=(out->arm_cap&15)+((out->arm_cap>>4)&15);
    if (!out->banks || out->banks>32) { return BLOCK_UNSUPPORTED; }
    uint32_t original; e=read_reg(r,arm->base+0x40,&original); if (e!=BLOCK_OK) { return e; }
    for (unsigned bank=0;bank<out->banks;++bank) {
        out->bank_changed=true;
        e=r->io->write32(r->io->context,arm->base+0x40,bank); if (e!=BLOCK_OK) { break; }
        uint32_t info; e=read_reg(r,arm->base+0x44,&info); if (e!=BLOCK_OK) { break; }
        uint32_t bytes=((info&127)+1)*((info&0x200) ? 1024 : 8192);
        if (out->ram_bytes>4*1024*1024-bytes) { e=BLOCK_RANGE; break; }
        out->bank_bytes[bank]=bytes; out->ram_bytes+=bytes;
    }
    /* Even a failed query attempts restoration. Transport failure may prevent
     * it: retain a visible bank_restored=no result and require reboot. */
    enum block_result restored=r->io->write32(r->io->context,arm->base+0x40,original);
    uint32_t check=0;
    if (restored==BLOCK_OK) { restored=read_reg(r,arm->base+0x40,&check); }
    out->bank_restored=restored==BLOCK_OK && check==original;
    if (!out->bank_restored) { return e!=BLOCK_OK ? e : restored==BLOCK_OK ? BLOCK_IO : restored; }
    if (e!=BLOCK_OK) { return e; }
    out->ram_base=0x198000; out->ram_valid=true; return BLOCK_OK;
}
enum block_result brcm_chip_discover(const struct brcm_chip_io *io,uint32_t chip_word,struct brcm_chip_info *out)
{
    if (!io || !io->read32 || !io->write32 || !out) { return BLOCK_ARGUMENT; }
    *out=(struct brcm_chip_info){.error=BLOCK_UNSUPPORTED};
    if ((chip_word&0xf00fffff)!=0x10064345) { return out->error; }
    out->attempted=true; struct reader r={.io=io,.out=out};
    enum block_result e=read_reg(&r,0x180000fc,&out->erom);
    if (e!=BLOCK_OK) { goto done; }
    if ((out->erom&3) || out->erom<0x18000000 || out->erom>0x181ffffc) { e=BLOCK_UNSUPPORTED; goto done; }
    e=components(&r); if (e!=BLOCK_OK) { goto done; }
    const struct brcm_core *arm=NULL; bool cc=false,sdio=false,d11=false;
    for (unsigned i=0;i<out->core_count;++i) {
        const struct brcm_core *c=&out->core[i];
        if (!core_address(c->base) || (c->wrapper && !core_address(c->wrapper))) { e=BLOCK_UNSUPPORTED; goto done; }
        if (c->id==0x800) { if (cc || c->base!=0x18000000) { e=BLOCK_UNSUPPORTED; goto done; } cc=true; }
        if (c->id==0x829) { if (sdio) { e=BLOCK_UNSUPPORTED; goto done; } sdio=true; }
        if (c->id==0x812) { d11=true; }
        if (c->id==0x83e) { if (arm || !c->wrapper || c->wrapper==c->base) { e=BLOCK_UNSUPPORTED; goto done; } arm=c; }
    }
    if (!cc || !sdio || !d11 || !arm) { e=BLOCK_UNSUPPORTED; goto done; }
    out->cores_valid=true; e=ram(&r,arm);
done:
    out->error=e; return e;
}
const char *brcm_core_name(uint16_t id)
{
    switch (id) {
    case 0x800: return "ChipCommon";
    case 0x812: return "D11 WLAN";
    case 0x829: return "SDIO device";
    case 0x83e: return "ARM CR4";
    case 0x83c: return "PCIe Gen2";
    case 0x827: return "PMU";
    case 0x840: return "GCI";
    default: return "other";
    }
}
