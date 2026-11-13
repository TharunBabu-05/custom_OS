/* AArch64 4 KiB stage-1 user leaves, Normal-NC AttrIndx 1. See Arm DDI0487
 * stage-1 table descriptors: AP=01 EL0 RW, AP=11 EL0 RO; PXN denies EL1
 * execution of user pages, UXN denies EL0 execution of data/stack pages. */
#include "user_mmu.h"
#define PAGE 4096ULL
#define ADDRESS_MASK 0x0000fffffffff000ULL
#define ROOT_SLOT ((USER_VA_BASE>>30)&511)
#define AF (1ULL<<10)
#define NG (1ULL<<11)
#define PXN (1ULL<<53)
#define UXN (1ULL<<54)
static bool aligned(uint64_t p) { return !(p&(PAGE-1)); }
static bool physical_page(uint64_t p) { return p && aligned(p) && p<=ADDRESS_MASK; }
static bool user_page(uint64_t va)
{
    return aligned(va) && ((va>=USER_VA_BASE && va-USER_VA_BASE<USER_IMAGE_PAGES*PAGE) ||
        (va>=USER_STACK_BASE && va-USER_STACK_BASE<USER_STACK_PAGES*PAGE));
}
static uint64_t flags(enum user_page_kind kind)
{
    return 3ULL|AF|NG|PXN|(3ULL<<8)|(1ULL<<2)|(1ULL<<6)|
        (kind==USER_PAGE_RW ? UXN : (1ULL<<7)|(kind==USER_PAGE_RO ? UXN : 0));
}
enum user_mmu_result user_mmu_build(struct user_tables *out,uint64_t physical,
    const uint64_t root[512],const struct user_mapping *maps,unsigned count)
{
    if (!out) { return USER_MMU_ARGUMENT; }out->ready=false;
    if (!root || !maps || !count || count>USER_MAPPING_LIMIT) { return USER_MMU_ARGUMENT; }
    if (!physical_page(physical) || physical>ADDRESS_MASK-(USER_TABLE_PAGES-1)*PAGE) { return USER_MMU_ADDRESS; }
    /* A private root cannot hide an existing kernel range. The existing
     * identity profile maps RAM/devices below this otherwise-empty slot. */
    if (root[ROOT_SLOT]) { return USER_MMU_COLLISION; }
    uintptr_t root_at=(uintptr_t)root,table_at=(uintptr_t)out->entries;
    if (root_at<table_at ? table_at-root_at<512*sizeof(uint64_t) : root_at-table_at<sizeof(out->entries)) { return USER_MMU_ARGUMENT; }
    struct user_mapping checked[USER_MAPPING_LIMIT];
    for (unsigned i=0;i<count;++i) { checked[i]=maps[i]; }
    for (unsigned i=0;i<count;++i) {
        const struct user_mapping *m=&checked[i];
        if (!user_page(m->virtual_address) || !physical_page(m->physical) ||
            (m->physical>=physical && m->physical-physical<USER_TABLE_PAGES*PAGE)) { return USER_MMU_ADDRESS; }
        if (m->kind!=USER_PAGE_RX && m->kind!=USER_PAGE_RO && m->kind!=USER_PAGE_RW) { return USER_MMU_PERMISSION; }
        if (m->virtual_address>=USER_STACK_BASE && m->kind!=USER_PAGE_RW) { return USER_MMU_PERMISSION; }
        for (unsigned j=0;j<i;++j) {
            if (m->virtual_address==checked[j].virtual_address || m->physical==checked[j].physical) { return USER_MMU_COLLISION; }
        }
    }
    for (unsigned i=0;i<512;++i) { out->entries[0][i]=root[i];out->entries[1][i]=0;out->entries[2][i]=0; }
    out->entries[0][ROOT_SLOT]=(physical+PAGE)|3;
    out->entries[1][0]=(physical+2*PAGE)|3;
    for (unsigned i=0;i<count;++i) {
        const struct user_mapping *m=&checked[i];out->entries[2][(m->virtual_address>>12)&511]=m->physical|flags(m->kind);
    }
    out->physical=physical;out->ready=true;return USER_MMU_OK;
}
bool user_mmu_lookup(const struct user_tables *t,uint64_t va,struct user_translation *out)
{
    if (!t || !out || !t->ready || !physical_page(t->physical) || !user_page(va&~(PAGE-1))) { return false; }
    if (t->physical>ADDRESS_MASK-(USER_TABLE_PAGES-1)*PAGE ||
        t->entries[0][ROOT_SLOT]!=((t->physical+PAGE)|3) || t->entries[1][0]!=((t->physical+2*PAGE)|3)) { return false; }
    uint64_t raw=t->entries[2][(va>>12)&511],attributes=raw&~ADDRESS_MASK;
    const enum user_page_kind kinds[]={USER_PAGE_RX,USER_PAGE_RO,USER_PAGE_RW};
    for (unsigned i=0;i<3;++i) {
        if (attributes==flags(kinds[i])) {
            uint64_t base=raw&ADDRESS_MASK;if (!physical_page(base)) { return false; }
            *out=(struct user_translation){base+(va&(PAGE-1)),raw,kinds[i]};return true;
        }
    }return false;
}
const char *user_mmu_result_text(enum user_mmu_result r)
{
    static const char *const messages[]={"ok","invalid user table argument","invalid user/backing/table address","user mapping collision","invalid user permission"};
    return (unsigned)r<sizeof(messages)/sizeof(messages[0]) ? messages[r] : "unknown user MMU error";
}
