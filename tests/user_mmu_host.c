#include "host_assert.h"
#include <string.h>
#include "user_mmu.h"
static struct user_tables table;
static uint64_t root[512],original[512];
static struct user_mapping maps[USER_MAPPING_LIMIT];
static unsigned count;
static void seed(void)
{
    memset(root,0,sizeof(root));
    /* Existing EL1-only identity-map root entries, unchanged by user build.
     * Their child trees need not be dereferenced by the private user walker. */
    root[0]=0x801003;root[1]=0x802003;root[64]=0x803003;root[112]=0x804003;root[511]=0x805003;
    memcpy(original,root,sizeof(root));count=0;
    for (unsigned i=0;i<USER_IMAGE_PAGES;++i) {
        maps[count++]=(struct user_mapping){USER_VA_BASE+(uint64_t)i*4096,0x40000000+(uint64_t)i*4096,i==0 ? USER_PAGE_RX : i<4 ? USER_PAGE_RO : USER_PAGE_RW};
    }
    for (unsigned i=0;i<USER_STACK_PAGES;++i) { maps[count++]=(struct user_mapping){USER_STACK_BASE+(uint64_t)i*4096,0x41000000+(uint64_t)i*4096,USER_PAGE_RW}; }
}
/* Raw independent descriptor oracle: AP, PXN, UXN, AttrIndx, AF, SH and nG. */
static uint64_t wanted(enum user_page_kind kind)
{
    uint64_t value=0x20000000000f47ULL;
    if (kind==USER_PAGE_RX || kind==USER_PAGE_RO) { value|=0x80; }
    if (kind!=USER_PAGE_RX) { value|=0x40000000000000ULL; }return value;
}
static void layout(void)
{
    seed();assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_OK && table.ready && table.physical==0x100000);
    assert(!memcmp(root,original,sizeof(root)));
    for (unsigned i=0;i<512;++i) { if (i!=128) { assert(table.entries[0][i]==root[i]); } }
    assert(table.entries[0][128]==0x101003 && table.entries[1][0]==0x102003);
    for (unsigned i=1;i<512;++i) { assert(!table.entries[1][i]); }
    for (unsigned i=0;i<count;++i) {
        const struct user_mapping *m=&maps[i];struct user_translation t={0};
        uint64_t raw=table.entries[2][(m->virtual_address>>12)&511];assert(raw==(m->physical|wanted(m->kind)));
        assert(user_mmu_lookup(&table,m->virtual_address+317,&t) && t.physical==m->physical+317 && t.kind==m->kind && t.descriptor==raw);
        assert(user_mmu_lookup(&table,m->virtual_address+4095,&t) && t.physical==m->physical+4095);
        assert((raw&(1ULL<<6)) && (raw&(1ULL<<53)) && ((raw>>2)&7)==1 && (raw&(1ULL<<10)) && (raw&(1ULL<<11)));
        assert(m->kind==USER_PAGE_RW ? (raw&(1ULL<<54)) && !(raw&(1ULL<<7)) : (raw&(1ULL<<7))!=0);
    }
    const uint64_t denied[]={0,0x80000,0x40000000,0x1000000000ULL,0x1c00030000ULL,USER_VA_BASE-1,
        USER_VA_BASE+0x8000,USER_VA_BASE+0xf000,USER_STACK_BASE-1,USER_STACK_BASE+0x4000,0x8000000000ULL,UINT64_MAX};
    for (unsigned i=0;i<sizeof(denied)/sizeof(denied[0]);++i) {
        struct user_translation got={.physical=0xfeed};assert(!user_mmu_lookup(&table,denied[i],&got) && got.physical==0xfeed);
    }
    puts("PASS: private user RX/RO/RW and stack pages have exact EL0 AP/PXN/UXN/Normal-NC/AF/nG flags; inherited kernel root unchanged; holes, guard and kernel/device pointers denied");
}
static void invalid(void)
{
    seed();assert(user_mmu_build(NULL,0x100000,root,maps,count)==USER_MMU_ARGUMENT);
    assert(user_mmu_build(&table,0x100000,NULL,maps,count)==USER_MMU_ARGUMENT && !table.ready);
    assert(user_mmu_build(&table,0x100000,root,NULL,count)==USER_MMU_ARGUMENT && !table.ready);
    assert(user_mmu_build(&table,0x100000,root,maps,0)==USER_MMU_ARGUMENT && !table.ready);
    assert(user_mmu_build(&table,0x100000,root,maps,13)==USER_MMU_ARGUMENT && !table.ready);
    const uint64_t bad_tables[]={0,1,0x100001,UINT64_MAX,0x0001000000000000ULL,0x0000ffffffffe000ULL};
    for (unsigned i=0;i<sizeof(bad_tables)/sizeof(bad_tables[0]);++i) { assert(user_mmu_build(&table,bad_tables[i],root,maps,count)==USER_MMU_ADDRESS && !table.ready); }
    const uint64_t bad_va[]={0,1,USER_VA_BASE-4096,USER_VA_BASE+1,USER_VA_BASE+8*4096,USER_VA_BASE+15*4096,
        USER_STACK_BASE+4*4096,0x8000000000ULL,UINT64_MAX};
    for (unsigned i=0;i<sizeof(bad_va)/sizeof(bad_va[0]);++i) {
        seed();maps[0].virtual_address=bad_va[i];assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_ADDRESS && !table.ready);
    }
    const uint64_t bad_pa[]={0,1,0x40000001,UINT64_MAX,0x0001000000000000ULL,0x100000,0x101000,0x102000};
    for (unsigned i=0;i<sizeof(bad_pa)/sizeof(bad_pa[0]);++i) {
        seed();maps[0].physical=bad_pa[i];assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_ADDRESS && !table.ready);
    }
    seed();maps[0].kind=(enum user_page_kind)3;assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_PERMISSION && !table.ready);
    seed();maps[11].kind=USER_PAGE_RX;assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_PERMISSION && !table.ready);
    seed();maps[1].physical=maps[0].physical;assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_COLLISION && !table.ready);
    seed();maps[1].virtual_address=maps[0].virtual_address;assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_COLLISION && !table.ready);
    seed();root[128]=1;assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_COLLISION && !table.ready);
    seed();memcpy(table.entries[0],root,sizeof(root));assert(user_mmu_build(&table,0x100000,table.entries[0],maps,count)==USER_MMU_ARGUMENT && !table.ready);
    assert(!memcmp(root,original,sizeof(root)));
    puts("PASS: malformed/table/backing/VA addresses, table-page aliases, duplicate user VA/physical aliases, root collision, invalid modes and executable stack refused; failures never publish ready");
}
static void corruption(void)
{
    seed();assert(user_mmu_build(&table,0x100000,root,maps,count)==USER_MMU_OK);struct user_translation got;
    uint64_t saved=table.entries[2][0];
    const unsigned bits[]={0,1,2,3,4,6,7,8,9,10,11,52,53,54,55,63};
    for (unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);++i) {
        table.entries[2][0]=saved^(1ULL<<bits[i]);bool ok=user_mmu_lookup(&table,USER_VA_BASE,&got);
        /* Adding UXN to an RX page is the valid, more restrictive RO kind;
         * all other single-bit flag changes here are malformed. */
        if (bits[i]==54) { assert(ok && got.kind==USER_PAGE_RO); }else { assert(!ok); }
    }
    table.entries[2][0]=saved;table.entries[0][128]^=0x1000;assert(!user_mmu_lookup(&table,USER_VA_BASE,&got));
    table.entries[0][128]^=0x1000;table.entries[1][0]^=0x1000;assert(!user_mmu_lookup(&table,USER_VA_BASE,&got));
    puts("PASS: user walker rejects malformed permissions and damaged private table links rather than granting broader access");
}
int main(void) { layout();invalid();corruption();return 0; }
