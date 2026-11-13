#ifndef OS_ELF_APP_H
#define OS_ELF_APP_H
#include <stdint.h>
#include <stddef.h>

#define APP_VA_BASE 0x2000000000ULL
enum { APP_PAGE_BYTES=4096,APP_IMAGE_PAGES=8,APP_STACK_PAGES=4,
       APP_ELF_MAX_BYTES=65536,APP_ELF_MAX_SEGMENTS=8 };
enum { APP_PF_X=1,APP_PF_W=2,APP_PF_R=4 };
struct app_segment { uint64_t vaddr,offset,filesz,memsz;uint32_t flags; };
struct app_image_plan {
    uint64_t entry,end;
    struct app_segment segments[APP_ELF_MAX_SEGMENTS];
    unsigned count;
};
enum app_elf_result { APP_ELF_OK,APP_ELF_ARGUMENT,APP_ELF_FORMAT,
    APP_ELF_BOUNDS,APP_ELF_SEGMENT,APP_ELF_ENTRY,APP_ELF_UNSUPPORTED };
/* Pure bounded parser; no allocation, copies, mappings or code execution.
 * ET_EXEC/ELF64LE AArch64 only. Program headers describe all loading;
 * section/debug metadata is deliberately ignored. Failure preserves *out.
 * Each PT_LOAD starts on a page and owns disjoint whole pages, with R,
 * R|X or R|W permissions. Writable executable pages are never accepted. */
enum app_elf_result app_elf_plan(const void *image,size_t bytes,struct app_image_plan *out);
const char *app_elf_error(enum app_elf_result result);
#endif
