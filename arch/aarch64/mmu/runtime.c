#include "mmu.h"
#include "console.h"
#include "cpu.h"
#include "heap.h"
#include "irq.h"
#include "task.h"
extern unsigned char __text_start[],__text_end[],__rodata_end[],__kernel_end[],__stack_guard[],__stack_bottom[],__stack_top[];
extern unsigned char mmu_probe_read_fault[],mmu_probe_read_resume[],mmu_probe_write_fault[],mmu_probe_write_resume[],mmu_probe_exec_resume[],mmu_text_marker[],mmu_rodata_marker[];
extern void mmu_arch_enable(uint64_t root,uint64_t tcr,uint64_t mair);
extern unsigned mmu_probe_read(uintptr_t address);
extern unsigned mmu_probe_write(uintptr_t address);
extern unsigned mmu_probe_exec(uintptr_t address);
static struct mmu_tables tables;
static struct mmu_plan plan;
static struct mmu_fault_expect expected;
static struct { const char *board;uintptr_t dtb;bool attempted,active;enum mmu_result result;uint64_t tcr,mmfr0,last_esr;unsigned tests,passed_mask;bool observed; } state;
enum { SMP_ROOT_SLOT=511, SMP_LEAF=2, SMP_PAGE_A=3, SMP_PAGE_B=4, SMP_PAGES=5 };
#define SMP_ALIAS_VA ((uint64_t)SMP_ROOT_SLOT<<30)
#define SMP_PATTERN 0x13579bdf2468ace0ULL
static uint64_t smp_root;
static uint64_t smp_allocation;
static unsigned smp_pages;
static bool same(const char *a,const char *b) { if (!a || !b) { return false; }while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static uint64_t sctlr(void) { uint64_t x;__asm__ volatile("mrs %0, sctlr_el1" : "=r"(x));return x; }
static uint64_t current_root(void) { uint64_t x;__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(x));return x; }
bool mmu_enabled(void) { return !!(sctlr()&1); }
static bool readback(void)
{
    uint64_t other,tcr,mair;__asm__ volatile("mrs %0, ttbr1_el1" : "=r"(other));__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));__asm__ volatile("mrs %0, mair_el1" : "=r"(mair));
    return current_root()==tables.physical && !other && tcr==state.tcr && mair==MMU_MAIR && (sctlr()&0x81005)==0x80001;
}
bool mmu_dma_compatible(void)
{ uint64_t control=sctlr();return !(control&0x1004) && (!(control&1) || (state.active && readback())); }
bool mmu_dma_buffer(uint64_t physical,size_t bytes)
{
    if (!bytes || bytes-1>UINT64_MAX-physical || !mmu_dma_compatible()) { return false; }
    return !mmu_enabled() || mmu_plan_range(&plan,physical,bytes,MMU_RW);
}
void mmu_setup(const char *board,uintptr_t dtb) { state.board=board;state.dtb=dtb; }
static void initialize(void)
{
    if (state.attempted || mmu_enabled()) { console_write("MMU activation already attempted or MMU already on; use mmuinfo. No table rebuild.\n");return; }
    if (irq_timer_running()) { console_write("MMU activation deferred: timerstop first, then mmuinit. No activation attempt consumed.\n");return; }
    state.attempted=true;state.result=MMU_STATE;uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(state.mmfr0));
    if (cpu_el()!=1 || (sctlr()&0x1005) || (daif&0x3c0)!=0x3c0) { goto failed; }
    enum mmu_profile profile;
    if (same(state.board,"qemu_virt")) { profile=MMU_QEMU; }
    else if (same(state.board,"rpi5-header") || same(state.board,"rpi5")) { profile=MMU_PI5; }
    else { state.result=MMU_ARGUMENT;goto failed; }
    struct memory_map map;if (!memory_boot_map(&map)) { state.result=MMU_RAM;goto failed; }
    const struct mmu_layout layout={
        (uintptr_t)__text_start,(uintptr_t)__text_end,(uintptr_t)__rodata_end,(uintptr_t)__kernel_end,
        (uintptr_t)__stack_guard,(uintptr_t)__stack_bottom,(uintptr_t)__stack_top,
        (uintptr_t)tables.entries,state.dtb,map.dtb_size,(uintptr_t)task_stacks,TASK_LIMIT
    };
    state.result=mmu_plan_make(&map,profile,&layout,&plan);if (state.result!=MMU_OK) { goto failed; }
    uint64_t end=plan.regions[plan.count-1].base+plan.regions[plan.count-1].size;
    if (!mmu_cpu_config(state.mmfr0,end,&state.tcr)) { state.result=MMU_CPU;goto failed; }
    state.result=mmu_tables_build(&tables,(uintptr_t)tables.entries,&plan);if (state.result!=MMU_OK) { goto failed; }
    console_write("Enabling EL1 MMU: identity mappings, RAM Normal-NC, devices nGnRnE; I/D caches stay off.\n");
    mmu_arch_enable(tables.physical,state.tcr,MMU_MAIR);state.active=readback();
    if (!state.active) { console_write("FATAL: MMU control readback differs from prepared policy.\n");cpu_halt(); }
    console_write("MMU enabled: kernel code RO executable; constants/DTB/tables RO XN; data RW XN; null/stack guard unmapped.\n");return;
failed:console_write("MMU activation refused before register changes: ");console_write(mmu_result_text(state.result));console_write(". Existing monitor remains available.\n");
}
bool mmu_ensure_enabled(void)
{
    if (!mmu_enabled()) { initialize(); }
    return state.active && readback();
}
bool mmu_enable_secondary(void)
{
    if (!state.active) { return false; }
    mmu_arch_enable(tables.physical,state.tcr,MMU_MAIR);
    return readback();
}

bool mmu_smp_prepare(void)
{
    if (smp_pages || !state.active || !readback() || tables.entries[0][SMP_ROOT_SLOT] ||
        !page_alloc_run(SMP_PAGES,&smp_allocation)) { return false; }
    smp_pages=SMP_PAGES;
    const size_t words=PAGE_SIZE/sizeof(uint64_t);
    uint64_t base=smp_allocation;
    if (!mmu_dma_buffer(base,SMP_PAGES*PAGE_SIZE)) { (void)mmu_smp_release();return false; }
    uint64_t *scratch=(uint64_t *)(uintptr_t)base;
    for (size_t i=0;i<words;++i) { scratch[i]=tables.entries[0][i]; }
    for (size_t i=words;i<SMP_PAGES*words;++i) { scratch[i]=0; }
    scratch[SMP_ROOT_SLOT]=(base+PAGE_SIZE)|3u;
    scratch[words]=(base+SMP_LEAF*PAGE_SIZE)|3u;
    scratch[SMP_LEAF*words]=mmu_page_descriptor(base+SMP_PAGE_A*PAGE_SIZE,MMU_RO);
    scratch[SMP_PAGE_A*words]=SMP_PATTERN;
    scratch[SMP_PAGE_B*words]=~SMP_PATTERN;
    __atomic_store_n(&smp_root,base,__ATOMIC_RELEASE);
    return true;
}

void mmu_smp_publish(bool second)
{
    uint64_t base=smp_allocation;
    uint64_t *leaf=(uint64_t *)(uintptr_t)(base+SMP_LEAF*PAGE_SIZE);
    __atomic_store_n(leaf,second ? mmu_page_descriptor(base+SMP_PAGE_B*PAGE_SIZE,MMU_RO) : 0,__ATOMIC_RELEASE);
    __asm__ volatile("dsb sy" ::: "memory");
}

bool mmu_smp_observe(enum mmu_smp_check check)
{
    if (check==MMU_SMP_RESTORE) { return mmu_smp_restore(); }
    uint64_t root=__atomic_load_n(&smp_root,__ATOMIC_ACQUIRE),par;
    const uint64_t va=SMP_ALIAS_VA;
    if (!root) { return false; }
    if (check==MMU_SMP_FIRST && current_root()==tables.physical) { mmu_switch_root(root); }
    if (current_root()!=root) { return false; }
    if (check==MMU_SMP_UNMAPPED) {
        __asm__ volatile("dsb sy; tlbi vaae1, %0; dsb sy; isb" :: "r"(va>>12) : "memory");
    } else {
        __asm__ volatile("dsb sy; isb" ::: "memory");
    }
    __asm__ volatile("at s1e1r, %1; isb; mrs %0, par_el1" : "=r"(par) : "r"(va) : "memory");
    if (check==MMU_SMP_UNMAPPED) { return (par&0x7f)==0x0f; } /* L3 translation fault. */
    bool second=check==MMU_SMP_SECOND;
    uint64_t page=root+(second ? SMP_PAGE_B : SMP_PAGE_A)*PAGE_SIZE;
    if ((par&(MMU_OUTPUT_MASK|1))!=page ||
        *(volatile const uint64_t *)(uintptr_t)va!=(second ? ~SMP_PATTERN : SMP_PATTERN)) { return false; }
    __asm__ volatile("at s1e1w, %1; isb; mrs %0, par_el1" : "=r"(par) : "r"(va) : "memory");
    if ((par&0x7f)!=0x1f) { return false; } /* L3 permission fault. */
    __asm__ volatile("at s1e0r, %1; isb; mrs %0, par_el1" : "=r"(par) : "r"(va) : "memory");
    return (par&0x7f)==0x1f;
}

bool mmu_smp_restore(void)
{
    uint64_t root=__atomic_load_n(&smp_root,__ATOMIC_ACQUIRE);
    if (!root) { return true; }
    if (current_root()!=tables.physical) { mmu_switch_root(tables.physical); }
    return current_root()==tables.physical;
}

bool mmu_smp_release(void)
{
    if (!mmu_smp_restore()) { return false; }
    if (__atomic_load_n(&smp_root,__ATOMIC_ACQUIRE)) {
        uint64_t *scratch=(uint64_t *)(uintptr_t)smp_allocation;
        for (size_t i=0;i<SMP_PAGES*PAGE_SIZE/sizeof(*scratch);++i) { scratch[i]=0; }
        __atomic_store_n(&smp_root,0,__ATOMIC_RELEASE);
    }
    while (smp_pages) {
        if (!page_free(smp_allocation+(uint64_t)(smp_pages-1)*PAGE_SIZE)) { return false; }
        --smp_pages;
    }
    return true;
}

static void information(void)
{
    console_write("MMU: ");console_write(!mmu_enabled() ? "off (run mmuinit)" : state.active && readback() ? "on; verified identity profile" : "on; UNRECOGNIZED profile");
    console_write("\nSCTLR_EL1=");console_hex(sctlr());console_write(" caches I/D=off in supported profile; timer IRQ opt-in.\n");
    if (!state.attempted) { return; }
    console_write("Activation=");console_write(mmu_result_text(state.result));console_write(" MMFR0=");console_hex(state.mmfr0);console_write(" TCR_EL1=");console_hex(state.tcr);
    console_write("\nTTBR0=");console_hex(tables.physical);console_write(" MAIR_EL1=");console_hex(MMU_MAIR);console_write(" tables_used=");console_u64(tables.used);console_write(" table_capacity=32 (kernel-reserved)\n");
    for (unsigned i=0;i<plan.count;++i) { console_write("MAP base=");console_hex(plan.regions[i].base);console_write(" bytes=");console_u64(plan.regions[i].size);console_write(" ");console_write(mmu_kind_text(plan.regions[i].kind));console_write("\n"); }
    console_write("Guard page=");console_hex((uintptr_t)__stack_guard);console_write(" null page=unmapped tests=");console_u64(state.tests);console_write(" last_pass_mask=");console_hex(state.passed_mask);console_write("\n");
}
uintptr_t mmu_handle_probe_abort(uint64_t esr,uintptr_t pc,uintptr_t far)
{
    if (!state.active || !mmu_fault_matches(&expected,esr,pc,far)) { return 0; }
    expected.armed=false;state.observed=true;state.last_esr=esr;return expected.resume;
}
static bool probe(uintptr_t address,bool write,bool execute,bool translation)
{
    state.observed=false;expected=(struct mmu_fault_expect){.write=write,.execute=execute,.translation=translation,.address=address,
        .pc=execute ? address : write ? (uintptr_t)mmu_probe_write_fault : (uintptr_t)mmu_probe_read_fault,
        .resume=execute ? (uintptr_t)mmu_probe_exec_resume : write ? (uintptr_t)mmu_probe_write_resume : (uintptr_t)mmu_probe_read_resume,.armed=true};
    __asm__ volatile("dsb sy" ::: "memory");unsigned fault=execute ? mmu_probe_exec(address) : write ? mmu_probe_write(address) : mmu_probe_read(address);
    expected.armed=false;console_write("probe=");console_write(execute ? "data execute" : write ? "read-only write" : "unmapped read");console_write(" address=");console_hex(address);console_write(" ESR=");console_hex(state.observed ? state.last_esr : 0);console_write(fault && state.observed ? " caught\n" : " NOT PROTECTED\n");return fault && state.observed;
}
static void test(void)
{
    if (!state.active || !readback()) { console_write("MMU test unavailable: run mmuinit successfully first.\n");return; }
    struct page_stats before=pages_stats();struct heap_stats heap=heap_get_stats();uint32_t *code=kmalloc(16);
    if (!code) { console_write("MMU test unavailable: no heap buffer.\n");return; }*code=0xd65f03c0u; /* RET if a broken XN policy permits execution. */
    __asm__ volatile("dsb sy; isb" ::: "memory");unsigned mask=0;
    if (probe(0,false,false,true)) { mask|=1; }
    if (probe((uintptr_t)__stack_guard,false,false,true)) { mask|=2; }
    if (probe((uintptr_t)mmu_text_marker,true,false,false)) { mask|=4; }
    if (probe((uintptr_t)mmu_rodata_marker,true,false,false)) { mask|=8; }
    if (probe((uintptr_t)code,false,true,false)) { mask|=16; }
    bool good=kfree(code);struct page_stats after=pages_stats();struct heap_stats final=heap_get_stats();
    good=good && mask==31 && before.free_pages==after.free_pages && before.allocated_pages==after.allocated_pages && heap.objects==final.objects && heap.pages==final.pages && heap.capacity_bytes==final.capacity_bytes && heap_check();
    ++state.tests;state.passed_mask=mask;console_write(good ? "PASS: MMU null/guard translation faults, code/constants write faults, data execution fault; heap/PMM counts restored.\n" : "FAIL: MMU protection tests.\n");
}
bool mmu_command(const char *line)
{
    if (same(line,"mmuinit")) { initialize();return true; }if (same(line,"mmuinfo")) { information();return true; }if (same(line,"mmutest")) { test();return true; }return false;
}
