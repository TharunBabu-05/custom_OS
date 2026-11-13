#ifndef OS_USER_MMU_H
#define OS_USER_MMU_H
#include <stdbool.h>
#include <stdint.h>

#define USER_VA_BASE 0x2000000000ULL
#define USER_STACK_BASE (USER_VA_BASE+0x10000ULL)
enum { USER_IMAGE_PAGES=8, USER_STACK_PAGES=4, USER_MAPPING_LIMIT=12, USER_TABLE_PAGES=3 };
enum user_page_kind { USER_PAGE_RX, USER_PAGE_RO, USER_PAGE_RW };
enum user_mmu_result { USER_MMU_OK, USER_MMU_ARGUMENT, USER_MMU_ADDRESS, USER_MMU_COLLISION, USER_MMU_PERMISSION };
struct user_mapping { uint64_t virtual_address,physical;enum user_page_kind kind; };
struct user_tables {
    _Alignas(4096) uint64_t entries[USER_TABLE_PAGES][512];
    uint64_t physical;
    bool ready;
};
struct user_translation { uint64_t physical,descriptor;enum user_page_kind kind; };
/* Pure 39-bit, 4 KiB stage-1 construction. entries[0] copies the verified
 * kernel root and adds a private three-level user subtree at USER_VA_BASE.
 * The caller supplies PMM-owned, zero-initialized backing RAM and validates
 * kernel_root as the active immutable EL1-only identity-map root. No page
 * allocation, CPU register access, cache maintenance or runtime table edits
 * occur here. The three table pages start at entries[0]; structure metadata
 * may occupy an extra alignment page and is not part of the hardware tables.
 * A failure marks the output not ready. Do not build into active tables. */
enum user_mmu_result user_mmu_build(struct user_tables *,uint64_t physical,
    const uint64_t kernel_root[512],const struct user_mapping *,unsigned count);
/* Lookup only the private user arena. Inherited kernel mappings deliberately
 * do not count as accessible user mappings. Output changes only on success. */
bool user_mmu_lookup(const struct user_tables *,uint64_t virtual_address,struct user_translation *);
const char *user_mmu_result_text(enum user_mmu_result);
#endif
