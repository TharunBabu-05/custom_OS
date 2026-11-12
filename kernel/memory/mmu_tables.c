/* Original 39-bit stage-1 identity policy and AArch64 4 KiB descriptors.
 * See docs/mmu.md for Arm descriptor/control-register references. */
#include "mmu.h"
#include "task.h"
#define ADDRESS_MASK 0x0000fffffffff000ULL
#define AF (1ULL<<10)
#define PXN (1ULL<<53)
#define UXN (1ULL<<54)
static bool aligned(uint64_t n) { return !(n&(PAGE_SIZE-1)); }
static bool span(uint64_t base,uint64_t bytes) { return bytes && base<MMU_VA_LIMIT && bytes<=MMU_VA_LIMIT-base; }
static uint64_t up(uint64_t n) { return (n+PAGE_SIZE-1)&~(PAGE_SIZE-1); }
static uint64_t attributes(enum mmu_kind k)
{
    if (k==MMU_DEVICE) { return AF|PXN|UXN|(2ULL<<8); }
    return AF|UXN|(3ULL<<8)|(1ULL<<2)|(k==MMU_RW ? PXN : 1ULL<<7)|(k==MMU_RO ? PXN : 0);
}
static bool normal(enum mmu_kind k) { return k==MMU_RW || k==MMU_RO || k==MMU_RX; }
static enum mmu_result add(struct mmu_plan *p,uint64_t base,uint64_t bytes,enum mmu_kind kind)
{
    if (!span(base,bytes) || !aligned(base) || !aligned(bytes) || kind<MMU_RW || kind>MMU_DEVICE) { return MMU_ARGUMENT; }
    if (p->count==MMU_MAX_REGIONS) { return MMU_LIMIT; }
    for (unsigned i=0;i<p->count;++i) { struct mmu_region r=p->regions[i];if (base<r.base+r.size && r.base<base+bytes) { return MMU_OVERLAP; } }
    p->regions[p->count++]=(struct mmu_region){base,bytes,kind};return MMU_OK;
}
static bool change(struct mmu_plan *p,uint64_t base,uint64_t bytes,enum mmu_kind kind)
{
    if (!span(base,bytes) || !aligned(base) || !aligned(bytes)) { return false; }
    for (unsigned i=0;i<p->count;++i) {
        struct mmu_region r=p->regions[i];
        if (r.kind!=MMU_RW || base<r.base || base-r.base>=r.size || bytes>r.size-(base-r.base)) { continue; }
        unsigned extra=(base>r.base)+(base+bytes<r.base+r.size)+(kind!=MMU_NONE);
        if (p->count-1+extra>MMU_MAX_REGIONS) { return false; }
        p->regions[i]=p->regions[--p->count];
        if (base>r.base) { p->regions[p->count++]=(struct mmu_region){r.base,base-r.base,MMU_RW}; }
        if (kind!=MMU_NONE) { p->regions[p->count++]=(struct mmu_region){base,bytes,kind}; }
        if (base+bytes<r.base+r.size) { p->regions[p->count++]=(struct mmu_region){base+bytes,r.base+r.size-base-bytes,MMU_RW}; }return true;
    }return false;
}
enum mmu_result mmu_plan_make(const struct memory_map *map,enum mmu_profile profile,const struct mmu_layout *l,struct mmu_plan *out)
{
    if (!map || !l || !out || !map->ram_count || map->ram_count>MEMORY_MAX_RAM || (profile!=MMU_PI5 && profile!=MMU_QEMU)) { return MMU_ARGUMENT; }
    uint64_t values[]={l->text_start,l->text_end,l->rodata_end,l->kernel_end,l->guard,l->stack_bottom,l->stack_top,l->tables};
    for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) { if (!aligned(values[i]) || values[i]>=MMU_VA_LIMIT) { return MMU_ARGUMENT; } }
    if (!l->text_start || l->text_start>=l->text_end || l->text_end>=l->rodata_end || l->rodata_end>l->tables ||
        MMU_TABLE_PAGES*PAGE_SIZE>MMU_VA_LIMIT-l->tables || l->tables+MMU_TABLE_PAGES*PAGE_SIZE>l->guard ||
        l->guard+PAGE_SIZE!=l->stack_bottom || l->stack_bottom>=l->stack_top || l->stack_top!=l->kernel_end ||
        !l->dtb || !span(l->dtb,l->dtb_bytes) || l->dtb_bytes<40 || l->dtb_bytes>0x200000 ||
        (l->dtb<l->kernel_end && l->text_start<l->dtb+l->dtb_bytes)) { return MMU_ARGUMENT; }
    struct mmu_plan p={0};uint64_t previous=0;
    for (unsigned i=0;i<map->ram_count;++i) {
        struct memory_range r=map->ram[i];if (!span(r.base,r.size) || (i && r.base<previous)) { return MMU_RAM; }previous=r.base+r.size;
        uint64_t first=up(r.base),end=previous&~(PAGE_SIZE-1);if (end<=first) { return MMU_RAM; }
        enum mmu_result result=add(&p,first,end-first,MMU_RW);if (result!=MMU_OK) { return result; }
    }
    if (!mmu_plan_range(&p,l->text_start,(size_t)(l->kernel_end-l->text_start),MMU_RW) ||
        !change(&p,l->text_start,l->text_end-l->text_start,MMU_RX) ||
        !change(&p,l->text_end,l->rodata_end-l->text_end,MMU_RO) ||
        !change(&p,l->tables,MMU_TABLE_PAGES*PAGE_SIZE,MMU_RO) || !change(&p,l->guard,PAGE_SIZE,MMU_NONE) ||
        !change(&p,l->dtb&~(PAGE_SIZE-1),up(l->dtb+l->dtb_bytes)-(l->dtb&~(PAGE_SIZE-1)),MMU_RO)) { return MMU_RAM; }
    /* Address zero remains unmapped even when the Pi's first RAM bank starts there. */
    if (map->ram[0].base==0 && !change(&p,0,PAGE_SIZE,MMU_NONE)) { return MMU_RAM; }
    if (l->task_count>TASK_LIMIT || (!!l->task_count!=!!l->task_base)) { return MMU_ARGUMENT; }
    if (l->task_count) {
        uint64_t bytes=(uint64_t)l->task_count*TASK_STACK_STRIDE;
        if (!aligned(l->task_base) || !span(l->task_base,bytes) || l->task_base<l->rodata_end || l->task_base+bytes>l->guard || !mmu_plan_range(&p,l->task_base,(size_t)bytes,MMU_RW)) { return MMU_ARGUMENT; }
        for (unsigned i=0;i<l->task_count;++i) { if (!change(&p,l->task_base+(uint64_t)i*TASK_STACK_STRIDE,TASK_GUARD_BYTES,MMU_NONE)) { return MMU_RAM; } }
    }
    const struct mmu_region pi[]={{0x1000000000ULL,0x80000000ULL,MMU_DEVICE},{0x1c00000000ULL,0x400000,MMU_DEVICE}};
    const struct mmu_region qemu[]={{0x08000000,0x04000000,MMU_DEVICE},{0x10000000,0x4000,MMU_DEVICE},{0x4010000000ULL,0x10000000,MMU_DEVICE}};
    const struct mmu_region *devices=profile==MMU_PI5 ? pi : qemu;unsigned count=profile==MMU_PI5 ? 2 : 3;
    for (unsigned i=0;i<count;++i) { enum mmu_result r=add(&p,devices[i].base,devices[i].size,MMU_DEVICE);if (r!=MMU_OK) { return r; } }
    for (unsigned i=1;i<p.count;++i) { struct mmu_region r=p.regions[i];unsigned j=i;while (j && p.regions[j-1].base>r.base) { p.regions[j]=p.regions[j-1];--j; }p.regions[j]=r; }
    *out=p;return MMU_OK;
}
bool mmu_plan_range(const struct mmu_plan *p,uint64_t base,size_t bytes,enum mmu_kind kind)
{
    if (!p || p->count>MMU_MAX_REGIONS || !span(base,bytes)) { return false; }
    uint64_t end=base+bytes;
    for (unsigned i=0;i<p->count && base<end;++i) { struct mmu_region r=p->regions[i];if (!span(r.base,r.size)) { return false; }if (r.kind==kind && base>=r.base && base-r.base<r.size) { uint64_t stop=r.base+r.size;base=stop<end ? stop : end; } }return base==end;
}
static uint64_t *child(struct mmu_tables *t,uint64_t *entry)
{
    if (!*entry) {
        if (t->used==MMU_TABLE_PAGES) { return NULL; }*entry=(t->physical+(uint64_t)t->used*PAGE_SIZE)|3;return t->entries[t->used++];
    }
    if ((*entry&3)!=3 || (*entry&ADDRESS_MASK)<t->physical) { return NULL; }
    uint64_t offset=(*entry&ADDRESS_MASK)-t->physical;if (!aligned(offset) || offset/PAGE_SIZE>=t->used) { return NULL; }return t->entries[offset/PAGE_SIZE];
}
enum mmu_result mmu_tables_build(struct mmu_tables *t,uint64_t physical,const struct mmu_plan *p)
{
    if (!t) { return MMU_ARGUMENT; }t->ready=false;
    if (!p || !p->count || p->count>MMU_MAX_REGIONS || !physical || !aligned(physical) || physical>ADDRESS_MASK-(MMU_TABLE_PAGES-1)*PAGE_SIZE) { return MMU_ARGUMENT; }
    uint64_t previous=0;
    for (unsigned i=0;i<p->count;++i) { struct mmu_region r=p->regions[i];if (!span(r.base,r.size) || !aligned(r.base) || !aligned(r.size) || (i && r.base<previous) || (!normal(r.kind) && r.kind!=MMU_DEVICE)) { return MMU_ARGUMENT; }previous=r.base+r.size; }
    for (unsigned i=0;i<MMU_TABLE_PAGES;++i) { for (unsigned j=0;j<512;++j) { t->entries[i][j]=0; } }t->physical=physical;t->used=1;
    for (unsigned i=0;i<p->count;++i) {
        struct mmu_region r=p->regions[i];uint64_t at=r.base,end=r.base+r.size;
        while (at<end) {
            uint64_t *entry=&t->entries[0][(at>>30)&511],bytes=1ULL<<30;
            if ((at&(bytes-1)) || end-at<bytes || *entry) {
                uint64_t *l2=child(t,entry);if (!l2) { return MMU_TABLE_FULL; }entry=&l2[(at>>21)&511];bytes=1ULL<<21;
                if ((at&(bytes-1)) || end-at<bytes || *entry) {
                    uint64_t *l3=child(t,entry);if (!l3) { return MMU_TABLE_FULL; }entry=&l3[(at>>12)&511];bytes=PAGE_SIZE;if (*entry) { return MMU_OVERLAP; }
                }
            }
            *entry=at|attributes(r.kind)|(bytes==PAGE_SIZE ? 3u : 1u);at+=bytes;
        }
    }t->ready=true;return MMU_OK;
}
bool mmu_lookup(const struct mmu_tables *t,uint64_t va,struct mmu_translation *out)
{
    if (!t || !out || !t->ready || !t->used || t->used>MMU_TABLE_PAGES || va>=MMU_VA_LIMIT) { return false; }
    unsigned table=0;
    for (unsigned level=1;level<=3;++level) {
        unsigned shift=level==1 ? 30 : level==2 ? 21 : 12;uint64_t d=t->entries[table][(va>>shift)&511];
        if (!(d&1)) { return false; }
        if ((d&3)==3 && level<3) {
            uint64_t base=d&ADDRESS_MASK;if (base<t->physical || (base-t->physical)/PAGE_SIZE>=t->used || (d&~(ADDRESS_MASK|3ULL))) { return false; }
            unsigned next=(unsigned)((base-t->physical)/PAGE_SIZE);if (!next || next==table) { return false; }table=next;continue;
        }
        if ((d&3)!=(level==3 ? 3u : 1u)) { return false; }
        uint64_t size=1ULL<<shift,base=d&ADDRESS_MASK;if (base&(size-1)) { return false; }
        enum mmu_kind kind=MMU_NONE;const enum mmu_kind candidates[]={MMU_RW,MMU_RX,MMU_RO,MMU_DEVICE};
        for (unsigned i=0;i<4;++i) { if ((d&~ADDRESS_MASK)==(attributes(candidates[i])|(level==3 ? 3u : 1u))) { kind=candidates[i]; } }
        if (kind==MMU_NONE) { return false; }*out=(struct mmu_translation){base+(va&(size-1)),d,kind,level};return true;
    }return false;
}
bool mmu_cpu_config(uint64_t mmfr0,uint64_t end,uint64_t *tcr)
{
    unsigned pa=(unsigned)(mmfr0&15);static const unsigned bits[]={32,36,40,42,44,48,52};
    if (!tcr || pa>6 || ((mmfr0>>28)&15) || !end || end>MMU_VA_LIMIT) { return false; }
    if (pa>5) { pa=5; }if (end>(1ULL<<bits[pa])) { return false; }
    *tcr=25ULL|(3ULL<<12)|(25ULL<<16)|(1ULL<<23)|(2ULL<<30)|((uint64_t)pa<<32);return true;
}
bool mmu_fault_matches(const struct mmu_fault_expect *e,uint64_t esr,uintptr_t pc,uintptr_t far)
{
    if (!e || !e->armed || !e->resume || e->pc!=pc || e->address!=far || (e->resume&3) || (esr&(1ULL<<10))) { return false; }
    unsigned ec=(unsigned)(esr>>26)&63,fsc=(unsigned)esr&63;
    if (ec!=(e->execute ? 0x21u : 0x25u) || (esr&(1ULL<<7)) || (!e->execute && !!(esr&(1ULL<<6))!=e->write)) { return false; }
    return e->translation ? !e->execute && !e->write && fsc>=4 && fsc<=7 : fsc>=13 && fsc<=15;
}
const char *mmu_result_text(enum mmu_result r)
{
    static const char *const names[]={"ok","invalid layout/argument","required RAM/layout missing","overlapping mappings","region limit","table pool exhausted","unsupported CPU/granule/physical address range","unexpected CPU state"};
    return (unsigned)r<sizeof(names)/sizeof(names[0]) ? names[r] : "unknown MMU error";
}
const char *mmu_kind_text(enum mmu_kind k)
{ switch (k) { case MMU_RW:return "Normal-NC EL1 RW XN";case MMU_RX:return "Normal-NC EL1 RO executable";case MMU_RO:return "Normal-NC EL1 RO XN";case MMU_DEVICE:return "Device-nGnRnE EL1 RW XN";default:return "unmapped"; } }
