#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "host_assert.h"
#include "mmu.h"
static struct mmu_tables *tables;
static struct mmu_layout layout(bool pi)
{
    uint64_t start=pi ? 0x80000 : 0x40200000;
    return (struct mmu_layout){.text_start=start,.text_end=start+0x40000,.rodata_end=start+0x60000,.kernel_end=start+0xb1000,
        .tables=start+0x70000,.guard=start+0xa0000,.stack_bottom=start+0xa1000,.stack_top=start+0xb1000,.dtb=pi ? 0x200000 : 0x40000000,.dtb_bytes=80606};
}
static struct memory_map ram(bool pi)
{
    struct memory_map map={0};
    if (pi) { map.ram[0]=(struct memory_range){0,0x3fc00000};map.ram[1]=(struct memory_range){0x40000000,0x1c0000000ULL};map.ram_count=2; }
    else { map.ram[0]=(struct memory_range){0x40000000,0x100000000ULL};map.ram_count=1; }return map;
}
/* Independent raw descriptor oracle; do not call the implementation walker. */
static bool oracle(uint64_t address,uint64_t *physical,unsigned *level,uint64_t *flags)
{
    if (!tables->ready || address>=0x8000000000ULL) { return false; }unsigned table=0;
    for (unsigned l=1;l<=3;++l) {
        unsigned shift=39-9*l;uint64_t raw=tables->entries[table][(address>>shift)%512];if (!(raw&1)) { return false; }
        if (l<3 && (raw&3)==3) { uint64_t base=raw&0x0000fffffffff000ULL;assert(base>=tables->physical && base-tables->physical<(uint64_t)tables->used*4096);table=(unsigned)((base-tables->physical)/4096);continue; }
        assert((raw&3)==(l==3 ? 3u : 1u));uint64_t bytes=1ULL<<shift;*physical=(raw&0x0000fffffffff000ULL)+(address&(bytes-1));*flags=raw&~0x0000fffffffff000ULL;*level=l;return true;
    }return false;
}
static enum mmu_kind plan_kind(const struct mmu_plan *p,uint64_t address)
{ for (unsigned i=0;i<p->count;++i) { if (address>=p->regions[i].base && address-p->regions[i].base<p->regions[i].size) { return p->regions[i].kind; } }return MMU_NONE; }
static void translation(const struct mmu_plan *p,uint64_t address)
{
    struct mmu_translation got={0};uint64_t physical=0,flags=0;unsigned level=0;enum mmu_kind kind=plan_kind(p,address);
    assert(mmu_lookup(tables,address,&got)==(kind!=MMU_NONE));assert(oracle(address,&physical,&level,&flags)==(kind!=MMU_NONE));
    if (kind!=MMU_NONE) {
        assert(got.physical==address && physical==address && got.kind==kind && got.level==level);
        uint64_t expected=kind==MMU_RW ? 0x0060000000000704ULL : kind==MMU_RX ? 0x0040000000000784ULL : kind==MMU_RO ? 0x0060000000000784ULL : 0x0060000000000600ULL;
        assert(flags==(expected|(level==3 ? 3u : 1u)));
    }
}
static void profiles(void)
{
    for (unsigned pi=0;pi<2;++pi) {
        struct memory_map m=ram(pi);struct mmu_layout l=layout(pi);struct mmu_plan p;
        assert(mmu_plan_make(&m,pi ? MMU_PI5 : MMU_QEMU,&l,&p)==MMU_OK && p.count<=MMU_MAX_REGIONS);
        assert(mmu_plan_range(&p,l.text_start,l.text_end-l.text_start,MMU_RX));assert(mmu_plan_range(&p,l.text_end,l.rodata_end-l.text_end,MMU_RO));
        assert(mmu_plan_range(&p,l.tables,MMU_TABLE_PAGES*PAGE_SIZE,MMU_RO));assert(mmu_plan_range(&p,l.stack_bottom,l.stack_top-l.stack_bottom,MMU_RW));
        assert(!mmu_plan_range(&p,l.guard,4096,MMU_RW) && !mmu_plan_range(&p,0,1,MMU_RW) && !mmu_plan_range(&p,l.text_end-1,2,MMU_RX));
        assert(mmu_plan_range(&p,l.dtb,l.dtb_bytes,MMU_RO));
        if (pi) { assert(mmu_plan_range(&p,0x1c00030000ULL,0x1000,MMU_DEVICE) && mmu_plan_range(&p,0x1001100000ULL,0x1000,MMU_DEVICE) && mmu_plan_range(&p,0x107d208000ULL,0x28,MMU_DEVICE)); }
        else { assert(mmu_plan_range(&p,0x0a000000,0x1000,MMU_DEVICE) && mmu_plan_range(&p,0x4010000000ULL,0x10000000,MMU_DEVICE)); }
        assert(mmu_tables_build(tables,l.tables,&p)==MMU_OK && tables->used<MMU_TABLE_PAGES);
        for (unsigned i=0;i<p.count;++i) { translation(&p,p.regions[i].base);translation(&p,p.regions[i].base+p.regions[i].size-1);translation(&p,p.regions[i].base+p.regions[i].size/2); }
        const uint64_t extra[]={0,4095,0x10000,0x3fc00000,0x3fffffff,0x40000000,0x100000000ULL,0x1ffffffffULL,0x200000000ULL,0x1c00400000ULL,0x401fffffffULL,0x4020000000ULL,MMU_VA_LIMIT-1,MMU_VA_LIMIT,UINT64_MAX};
        for (unsigned i=0;i<sizeof(extra)/sizeof(extra[0]);++i) { translation(&p,extra[i]); }translation(&p,l.guard);translation(&p,l.guard+4095);
        uint32_t random=9123;
        for (unsigned i=0;i<20000;++i) { random=random*1664525u+1013904223u;uint64_t a=((uint64_t)(random%128)<<30)|(random&0x3fffffff);translation(&p,a); }
        if (pi) { struct mmu_translation t;assert(mmu_lookup(tables,0x100000000ULL,&t) && t.level==1 && t.kind==MMU_RW); }
    }
    puts("PASS: Pi 8 GiB and QEMU identity policy, exact independent AP/AF/SH/AttrIndx/PXN/UXN descriptors at all levels, RAM/MMIO/DTB/table/code/data permissions, guard holes and 40000 sampled translations");
}
static void invalid(void)
{
    struct memory_map m=ram(true);struct mmu_layout l=layout(true);struct mmu_plan p,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));
    for (unsigned i=0;i<14;++i) {
        m=ram(true);l=layout(true);p=sentinel;
        if (!i) { m.ram_count=17; }if (i==1) { m.ram_count=0; }if (i==2) { m.ram[1].base=0x100000; }if (i==3) { m.ram[1].size=UINT64_MAX; }
        if (i==4) { l.text_start++; }if (i==5) { l.text_end=l.text_start; }if (i==6) { l.dtb=l.text_start; }if (i==7) { l.dtb=MMU_VA_LIMIT-8; }
        if (i==8) { l.dtb_bytes=UINT64_MAX; }if (i==9) { l.tables=l.guard; }if (i==10) { l.stack_top=MMU_VA_LIMIT-4096;l.kernel_end=l.stack_top; }
        if (i==11) { l.stack_bottom++; }if (i==12) { m.ram[1].base=0x1000000000ULL;m.ram[1].size=0x80000000; }if (i==13) { l.dtb=0x3ffff000; }
        assert(mmu_plan_make(&m,MMU_PI5,&l,&p)!=MMU_OK && !memcmp(&p,&sentinel,sizeof(p)));
    }
    assert(mmu_plan_make(NULL,MMU_PI5,&l,&p)==MMU_ARGUMENT && mmu_plan_make(&m,(enum mmu_profile)99,&l,&p)==MMU_ARGUMENT);
    p=(struct mmu_plan){.count=20};for (unsigned i=0;i<p.count;++i) { p.regions[i]=(struct mmu_region){((uint64_t)i+1)<<30,4096,MMU_RW}; }
    assert(mmu_tables_build(tables,0x100000,&p)==MMU_TABLE_FULL && !tables->ready && tables->used==32);struct mmu_translation out;assert(!mmu_lookup(tables,1ULL<<30,&out));
    p=(struct mmu_plan){.count=1,.regions={{0x40000000,0x100000000,MMU_RW}}};assert(mmu_tables_build(tables,0x100000,&p)==MMU_OK);
    uint64_t saved=tables->entries[0][1];tables->entries[0][1]=0x100003;assert(!mmu_lookup(tables,0x40000000,&out));tables->entries[0][1]=saved|(1ULL<<11);assert(!mmu_lookup(tables,0x40000000,&out));tables->entries[0][1]=saved;
    p.regions[0].size=UINT64_MAX;assert(mmu_tables_build(tables,0x100000,&p)==MMU_ARGUMENT && !tables->ready);
    puts("PASS: invalid/overlapping/out-of-RAM/overflow layouts preserve plan output, table exhaustion never publishes partial mappings, malformed descriptors and table cycles rejected");
}
static void controls_and_faults(void)
{
    uint64_t tcr=0;assert(mmu_cpu_config(2,0x4020000000ULL,&tcr) && tcr==0x280993019ULL);assert(mmu_cpu_config(6,MMU_VA_LIMIT,&tcr) && tcr==0x580993019ULL);
    assert(!mmu_cpu_config(1,0x4020000000ULL,&tcr) && !mmu_cpu_config(15,1,&tcr) && !mmu_cpu_config(2|(15ULL<<28),1,&tcr) && !mmu_cpu_config(2,MMU_VA_LIMIT+1,&tcr) && !mmu_cpu_config(2,0,&tcr));
    const struct mmu_fault_expect base={.armed=true,.pc=0x80000,.address=0x90000,.resume=0x80100};
    for (unsigned mode=0;mode<3;++mode) {
        struct mmu_fault_expect e=base;e.translation=!mode;e.write=mode==1;e.execute=mode==2;uint64_t esr=((uint64_t)(e.execute ? 0x21 : 0x25)<<26)|(1ULL<<25)|(e.write ? 64 : 0)|(e.translation ? 7 : 15);
        assert(mmu_fault_matches(&e,esr,e.pc,e.address));
        assert(!mmu_fault_matches(&e,esr,e.pc+4,e.address) && !mmu_fault_matches(&e,esr,e.pc,e.address+8) && !mmu_fault_matches(&e,esr|(1ULL<<10),e.pc,e.address) && !mmu_fault_matches(&e,esr|(1ULL<<7),e.pc,e.address));
        assert(!mmu_fault_matches(&e,0xf2000042,e.pc,e.address));e.armed=false;assert(!mmu_fault_matches(&e,esr,e.pc,e.address));
    }
    puts("PASS: CPU 4K/PA/TCR checks, TTBR1 disabled, no cache enable, exact PC/FAR/EC/FSC/write matching and refusal of BRK/unarmed/unrelated faults");
}
static void task_guards(void)
{
    struct memory_map m=ram(true);struct mmu_layout l=layout(true);struct mmu_plan p;
    l.guard=l.text_start+0xb0000;l.stack_bottom=l.guard+4096;l.stack_top=l.kernel_end=l.stack_bottom+65536;
    l.task_base=l.text_start+0x90000;l.task_count=4;
    assert(mmu_plan_make(&m,MMU_PI5,&l,&p)==MMU_OK && mmu_tables_build(tables,l.tables,&p)==MMU_OK);
    for (unsigned i=0;i<4;++i) {
        uint64_t base=l.task_base+(uint64_t)i*20480;
        assert(!mmu_plan_range(&p,base,4096,MMU_RW) && mmu_plan_range(&p,base+4096,16384,MMU_RW));
        translation(&p,base);translation(&p,base+4095);translation(&p,base+4096);translation(&p,base+20479);
    }
    const uint64_t bad_base[]={l.text_start,l.tables,l.guard,MMU_VA_LIMIT-4096,l.task_base+1};
    for (unsigned i=0;i<sizeof(bad_base)/sizeof(bad_base[0]);++i) { l.task_base=bad_base[i];assert(mmu_plan_make(&m,MMU_PI5,&l,&p)!=MMU_OK); }
    l=layout(true);l.task_count=5;l.task_base=l.text_start+0x90000;assert(mmu_plan_make(&m,MMU_PI5,&l,&p)!=MMU_OK);
    puts("PASS: independent task stack guards unmapped, full stack extents RW/XN, adjacent byte boundaries and invalid/overlapping/out-of-kernel task arenas refused");
}
int main(void)
{
    SYSTEM_INFO system;GetSystemInfo(&system);size_t rounded=(sizeof(*tables)+4095)&~(size_t)4095;
    unsigned char *allocation=VirtualAlloc(NULL,rounded+4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(allocation && system.dwPageSize==4096);DWORD old;assert(VirtualProtect(allocation+rounded,4096,PAGE_NOACCESS,&old));tables=(struct mmu_tables *)allocation;
    profiles();invalid();controls_and_faults();task_guards();assert(VirtualFree(allocation,0,MEM_RELEASE));return 0;
}
