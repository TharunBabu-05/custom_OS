#include "arm_platform.h"
#include "console.h"
#include "irq.h"
#include "mmu.h"

#define PSCI_AFFINITY_INFO 0xc4000004u

static bool equal(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}

static void topology(const void *dtb,bool query)
{
    struct arm_topology discovered;
    uint64_t boot;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(boot));
    boot&=ARM_AFFINITY_MASK;
    console_write("Running CPU affinity="); console_hex(boot); console_write("\n");
    if (!arm_topology_read(dtb,0x200000,&discovered)) {
        console_write("CPU topology unavailable.\n");
        return;
    }
    enum psci_conduit method=psci_discover(dtb,0x200000);
    if (query && method!=PSCI_NONE && psci_call(method,PSCI_AFFINITY_INFO,boot)!=0) {
        console_write("PSCI affinity query unavailable on running CPU.\n");
        method=PSCI_NONE;
    }
    console_write("DT CPUs="); console_u64(discovered.count);
    console_write(method==PSCI_HVC ? " PSCI=hvc\n" : method==PSCI_SMC ? " PSCI=smc\n" : " PSCI=unavailable\n");
    for (unsigned i=0;i<discovered.count;++i) {
        const struct arm_cpu *cpu=&discovered.cpu[i];
        console_write("CPU affinity="); console_hex(cpu->affinity);
        console_write(cpu->enabled ? " dt=enabled" : " dt=disabled");
        if (cpu->affinity==boot) { console_write(" current"); }
        if (query && cpu->enabled && method!=PSCI_NONE && (cpu->psci || cpu->affinity==boot)) {
            int32_t state=cpu->affinity==boot ? 0 : psci_call(method,PSCI_AFFINITY_INFO,cpu->affinity);
            console_write(state==0 ? " psci=on" : state==1 ? " psci=off" :
                          state==2 ? " psci=pending" : " psci=unavailable");
        }
        console_write("\n");
    }
}

static uint64_t cache_size_id(uint64_t selector)
{
    uint64_t old,value;
    uint64_t irq=irq_save();
    __asm__ volatile("mrs %0, csselr_el1" : "=r"(old));
    __asm__ volatile("msr csselr_el1, %0; isb" :: "r"(selector) : "memory");
    __asm__ volatile("mrs %0, ccsidr_el1" : "=r"(value));
    __asm__ volatile("msr csselr_el1, %0; isb" :: "r"(old) : "memory");
    irq_restore(irq);
    return value;
}

static void cache_row(unsigned level,const char *name,bool instruction,bool extended)
{
    uint64_t id=cache_size_id(2*level+instruction);
    unsigned ways=extended ? (id>>3)&0x1fffff : (id>>3)&0x3ff;
    unsigned sets=extended ? (id>>32)&0xffffff : (id>>13)&0x7fff;
    console_write("L"); console_u64(level+1); console_write(name);
    console_write(" line_bytes="); console_u64(16u<<(id&7));
    console_write(" sets="); console_u64(sets+1);
    console_write(" ways="); console_u64(ways+1);
    console_write("\n");
}

static void cache_info(void)
{
    uint64_t ctr,clidr,mmfr2,sctlr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    __asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
    __asm__ volatile("mrs %0, id_aa64mmfr2_el1" : "=r"(mmfr2));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    console_write(sctlr&(1u<<12) ? "Cache I=on" : "Cache I=off");
    console_write(sctlr&4 ? " D=on" : " D=off");
    console_write(" Imin_bytes="); console_u64(4u<<(ctr&15));
    console_write(" Dmin_bytes="); console_u64(4u<<((ctr>>16)&15));
    console_write("\n");
    unsigned ccidx=(mmfr2>>20)&15;
    if (ccidx>1) { console_write("Cache geometry unavailable.\n"); return; }
    for (unsigned level=0;level<7;++level) {
        unsigned type=(clidr>>(3*level))&7;
        if (!type || type>4) { break; }
        if (type==1 || type==3) { cache_row(level," instruction",true,ccidx!=0); }
        if (type>=2) { cache_row(level,type==4 ? " unified" : " data",false,ccidx!=0); }
    }
}

static void atomic_test(void)
{
    static _Alignas(8) uint64_t word;
    if (!mmu_enabled() || !mmu_dma_buffer((uintptr_t)&word,sizeof(word))) {
        console_write("Run mmuinit before atomictest.\n");
        return;
    }
    __atomic_store_n(&word,7,__ATOMIC_RELEASE);
#ifdef OUROS_PI5
    /* The BCM2712 uncached path can abort on an exclusive transaction. */
    bool pass=__atomic_load_n(&word,__ATOMIC_ACQUIRE)==7;
    __atomic_store_n(&word,0,__ATOMIC_RELEASE);
    pass=pass && __atomic_load_n(&word,__ATOMIC_ACQUIRE)==0;
    console_write(pass ? "PASS: boot CPU acquire/release. Exclusive RMW requires a cacheable test mapping on Pi.\n" :
                         "FAIL: boot CPU acquire/release.\n");
#else
    uint64_t expected=6;
    bool rejected=!__atomic_compare_exchange_n(&word,&expected,11,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE) &&
                  expected==7 && __atomic_load_n(&word,__ATOMIC_ACQUIRE)==7;
    expected=7;
    bool replaced=__atomic_compare_exchange_n(&word,&expected,11,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    uint64_t previous=__atomic_fetch_add(&word,5,__ATOMIC_ACQ_REL);
    bool pass=rejected && replaced && previous==11 && __atomic_load_n(&word,__ATOMIC_ACQUIRE)==16;
    __atomic_store_n(&word,0,__ATOMIC_RELEASE);
    console_write(pass ? "PASS: boot CPU acquire/release, compare-exchange and fetch-add.\n" :
                         "FAIL: boot CPU atomic operations.\n");
#endif
}

bool arm_platform_command(const char *line,const void *dtb)
{
    if (equal(line,"cpulist") || equal(line,"cpustate")) { topology(dtb,line[3]=='s'); return true; }
    if (equal(line,"cacheinfo")) { cache_info(); return true; }
    if (equal(line,"atomictest")) { atomic_test(); return true; }
    return false;
}
