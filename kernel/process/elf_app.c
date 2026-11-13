/* Original narrow static-executable loader policy. File fields follow the
 * ELF gABI and Arm AAELF64 (https://github.com/ARM-software/abi-aa/aaelf64).
 * All untrusted file reads are byte-based, bounded before access; no packed
 * host structs, host alignment assumptions or untrusted pointer arithmetic. */
#include "elf_app.h"
#include <stdbool.h>
static uint16_t u16(const unsigned char *p) { return (uint16_t)p[0]|(uint16_t)p[1]<<8; }
static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t u64(const unsigned char *p) { return u32(p)|(uint64_t)u32(p+4)<<32; }
static bool file_range(uint64_t at,uint64_t n,size_t bytes) { return at<=bytes && n<=bytes-at; }
static uint64_t rounded(uint64_t n) { return (n+APP_PAGE_BYTES-1)&~(uint64_t)(APP_PAGE_BYTES-1); }
enum app_elf_result app_elf_plan(const void *image,size_t bytes,struct app_image_plan *out)
{
    if (!image || !out) { return APP_ELF_ARGUMENT; }
    if (bytes<64 || bytes>APP_ELF_MAX_BYTES) { return APP_ELF_BOUNDS; }
    const unsigned char *p=image;
    if (p[0]!=0x7f || p[1]!='E' || p[2]!='L' || p[3]!='F' || p[4]!=2 || p[5]!=1 || p[6]!=1 ||
        p[7] || p[8] || u16(p+16)!=2 || u16(p+18)!=183 || u32(p+20)!=1 ||
        u32(p+48) || u16(p+52)!=64 || u16(p+54)!=56) { return APP_ELF_FORMAT; }
    for (unsigned i=9;i<16;++i) { if (p[i]) { return APP_ELF_FORMAT; } }
    uint64_t at=u64(p+32);unsigned count=u16(p+56);
    if (!count || count>32 || at<64 || !file_range(at,(uint64_t)count*56,bytes)) { return APP_ELF_BOUNDS; }
    struct app_image_plan plan={.entry=u64(p+24),.end=APP_VA_BASE};bool executable_entry=false;
    if (plan.entry&3) { return APP_ELF_ENTRY; }
    for (unsigned i=0;i<count;++i) {
        const unsigned char *h=p+(size_t)at+i*56;uint32_t type=u32(h),flags=u32(h+4);
        uint64_t offset=u64(h+8),va=u64(h+16),filesz=u64(h+32),memsz=u64(h+40),align=u64(h+48);
        if (type==0) { continue; } /* PT_NULL */
        if (type==6) { /* PT_PHDR: metadata only; never a second copy. */
            if (!file_range(offset,filesz,bytes) || filesz>memsz) { return APP_ELF_BOUNDS; }continue;
        }
        if (type==0x6474e551) { /* PT_GNU_STACK: stack policy is fixed RW/NX. */
            if (flags!=(APP_PF_R|APP_PF_W) || filesz || memsz) { return APP_ELF_UNSUPPORTED; }continue;
        }
        if (type!=1) { return APP_ELF_UNSUPPORTED; } /* No interpreter/dynamic/TLS/notes contract. */
        if (plan.count==APP_ELF_MAX_SEGMENTS) { return APP_ELF_SEGMENT; }
        if (flags!=APP_PF_R && flags!=(APP_PF_R|APP_PF_X) && flags!=(APP_PF_R|APP_PF_W)) { return APP_ELF_SEGMENT; }
        if (!memsz || filesz>memsz || va<APP_VA_BASE || va>=APP_VA_BASE+(uint64_t)APP_IMAGE_PAGES*APP_PAGE_BYTES ||
            memsz>APP_VA_BASE+(uint64_t)APP_IMAGE_PAGES*APP_PAGE_BYTES-va || (va&(APP_PAGE_BYTES-1))) { return APP_ELF_SEGMENT; }
        if (!file_range(offset,filesz,bytes)) { return APP_ELF_BOUNDS; }
        if (align>APP_PAGE_BYTES || (align>1 && ((align&(align-1)) || (va&(align-1))!=(offset&(align-1))))) { return APP_ELF_SEGMENT; }
        uint64_t end=va+rounded(memsz);
        for (unsigned j=0;j<plan.count;++j) {
            const struct app_segment *s=&plan.segments[j];uint64_t prior_end=s->vaddr+rounded(s->memsz);
            if (va<prior_end && s->vaddr<end) { return APP_ELF_SEGMENT; }
        }
        if ((flags&APP_PF_X) && plan.entry>=va && plan.entry-va<filesz) { executable_entry=true; }
        if (end>plan.end) { plan.end=end; }
        plan.segments[plan.count++]=(struct app_segment){va,offset,filesz,memsz,flags};
    }
    if (!plan.count || !executable_entry) { return APP_ELF_ENTRY; }
    /* Stable ordering simplifies mapping and deterministic validation/tests. */
    for (unsigned i=1;i<plan.count;++i) {
        struct app_segment s=plan.segments[i];unsigned j=i;
        while (j && plan.segments[j-1].vaddr>s.vaddr) { plan.segments[j]=plan.segments[j-1];--j; }plan.segments[j]=s;
    }
    *out=plan;return APP_ELF_OK;
}
const char *app_elf_error(enum app_elf_result r)
{
    static const char *const text[]={"ok","invalid ELF argument","unsupported ELF header",
        "ELF file bounds","invalid ELF load segment","invalid ELF entry","unsupported ELF program header"};
    return (unsigned)r<sizeof(text)/sizeof(text[0]) ? text[r] : "unknown ELF error";
}
