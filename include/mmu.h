#ifndef OS_MMU_H
#define OS_MMU_H
#include "memory.h"
#define MMU_VA_LIMIT (1ULL<<39)
#define MMU_MAIR 0xff4400ULL /* Device-nGnRnE, Normal NC, Normal WB. */
#define MMU_OUTPUT_MASK 0x0000fffffffff000ULL
enum { MMU_MAX_REGIONS=64, MMU_TABLE_PAGES=32 };
enum mmu_kind { MMU_NONE,MMU_RW,MMU_RX,MMU_RO,MMU_DEVICE,MMU_DMA_WB };
enum mmu_profile { MMU_QEMU,MMU_PI5 };
enum mmu_result { MMU_OK,MMU_ARGUMENT,MMU_RAM,MMU_OVERLAP,MMU_LIMIT,MMU_TABLE_FULL,MMU_CPU,MMU_STATE };
struct mmu_region { uint64_t base,size;enum mmu_kind kind; };
struct mmu_plan { struct mmu_region regions[MMU_MAX_REGIONS];unsigned count; };
struct mmu_layout {
    uint64_t text_start,text_end,rodata_end,kernel_end,guard,stack_bottom,stack_top;
    uint64_t tables,dtb,dtb_bytes;
    uint64_t task_base;unsigned task_count;
    uint64_t dma_scratch;
};
struct mmu_tables {
    _Alignas(4096) uint64_t entries[MMU_TABLE_PAGES][512];
    uint64_t physical;unsigned used;bool ready;
};
struct mmu_translation { uint64_t physical,descriptor;enum mmu_kind kind;unsigned level; };
struct mmu_fault_expect { bool armed,write,execute,translation;uintptr_t pc,address,resume; };
/* Pure bounded identity-map policy and descriptor construction, host-testable.
 * A failed plan preserves output; failed table builds are marked not ready.
 * Tables are built before activation and remain immutable afterward. */
enum mmu_result mmu_plan_make(const struct memory_map *,enum mmu_profile,const struct mmu_layout *,struct mmu_plan *);
enum mmu_result mmu_tables_build(struct mmu_tables *,uint64_t physical,const struct mmu_plan *);
bool mmu_lookup(const struct mmu_tables *,uint64_t virtual_address,struct mmu_translation *);
uint64_t mmu_page_descriptor(uint64_t physical,enum mmu_kind);
bool mmu_plan_range(const struct mmu_plan *,uint64_t base,size_t bytes,enum mmu_kind);
bool mmu_cpu_config(uint64_t mmfr0,uint64_t largest_end,uint64_t *tcr);
bool mmu_fault_matches(const struct mmu_fault_expect *,uint64_t esr,uintptr_t pc,uintptr_t far);
const char *mmu_result_text(enum mmu_result);
const char *mmu_kind_text(enum mmu_kind);
/* AArch64 runtime with explicit activation via startup or the monitor. */
bool mmu_enabled(void);
bool mmu_ensure_enabled(void);
bool mmu_enable_secondary(void);
void mmu_switch_root(uint64_t physical);
/* Invalidate stage-1 translations for the shared EL1 regime on all cores. */
void mmu_tlb_sync(void);
bool mmu_dma_compatible(void);
bool mmu_dma_buffer(uint64_t physical,size_t bytes);
/* Only the exclusively owned WB scratch page is eligible. While active,
 * callers must serialize device ownership and keep boot-CPU IRQs masked. */
unsigned mmu_dma_cache_begin(uint64_t physical);
void mmu_dma_cache_handoff(uint64_t physical,unsigned stride);
void mmu_dma_cache_receive(uint64_t physical,unsigned stride);
void mmu_dma_cache_end(uint64_t physical,unsigned stride);
void mmu_setup(const char *board,uintptr_t dtb);
bool mmu_command(const char *line);
uintptr_t mmu_handle_probe_abort(uint64_t esr,uintptr_t pc,uintptr_t far);

/* The boot CPU owns allocation and PTE writes. Every attached CPU
 * must acknowledge restoring its root or be off before release. */
enum mmu_smp_check { MMU_SMP_FIRST,MMU_SMP_UNMAPPED,MMU_SMP_SECOND,MMU_SMP_RESTORE };
bool mmu_smp_prepare(void);
void mmu_smp_publish(bool second);
bool mmu_smp_observe(enum mmu_smp_check);
bool mmu_smp_restore(void);
bool mmu_smp_release(void);
#endif
