#include "heap.h"
#include "memory.h"
#include "console.h"
#include "mmu.h"
static void *slots[16];
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool prefix(const char *p,const char *q) { while (*q) { if (*p++!=*q++) { return false; } }return true; }
static bool decimal(const char *p,unsigned limit,unsigned *out)
{
    if (!*p) { return false; }unsigned v=0;
    for (;*p;++p) { if (*p<'0' || *p>'9' || v>limit/10 || (v==limit/10 && (unsigned)(*p-'0')>limit%10)) { return false; }v=v*10+(unsigned)(*p-'0'); }
    *out=v;return true;
}
static void information(void)
{
    struct heap_stats s=heap_get_stats();console_write("Kernel heap: ");console_write(!s.ready ? "disabled" : s.poisoned ? "FAILED (PMM ownership/release error)" : heap_check() ? "ready" : "INVALID metadata");
    console_write("\npages=");console_u64(s.pages);console_write(" objects=");console_u64(s.objects);console_write(" capacity_bytes=");console_u64(s.capacity_bytes);console_write("\nBacking RAM: ");console_memory_size((uint64_t)s.pages*PAGE_SIZE);
    console_write("\npeak_pages=");console_u64(s.peak_pages);console_write(" peak_objects=");console_u64(s.peak_objects);console_write(" allocations=");console_u64(s.allocations);console_write(" frees=");console_u64(s.frees);
    console_write(" failures=");console_u64(s.failures);console_write(" rejected_frees=");console_u64(s.rejected_frees);console_write("\n");
    for (unsigned i=0;i<HEAP_CLASSES;++i) { if (s.classes[i].pages) { console_write("class_bytes=");console_u64(s.classes[i].size);console_write(" pages=");console_u64(s.classes[i].pages);console_write(" objects=");console_u64(s.classes[i].objects);console_write("\n"); } }
    console_write("16-byte alignment; allocation limit=4096; page limit=128 (512 KiB).\nCapacity counts rounded live buffers; it is not free RAM. Empty pages return to PMM.\nSingle core; MMU ");console_write(mmu_enabled() ? "on" : "off");console_write("; not an IRQ, user-space or DMA allocator.\n");
}
static void test(void)
{
    struct heap_stats before=heap_get_stats();struct page_stats pbefore=pages_stats();
    if (!heap_check()) { console_write("Heap test unavailable: heap disabled or invalid.\n");return; }
    const unsigned sizes[]={1,17,33,65,129,257,513,1025,2049,4096};void *blocks[10]={0};bool good=true;
    for (unsigned i=0;i<10;++i) {
        blocks[i]=kmalloc(sizes[i]);if (!blocks[i] || ((uintptr_t)blocks[i]&15)) { good=false;break; }
        unsigned char *p=blocks[i];size_t cap=heap_usable_size(p);for (size_t j=0;j<cap;++j) { if (p[j]) { good=false; }p[j]=(unsigned char)(i+j+1); }
    }
    for (unsigned i=0;i<10;++i) { if (blocks[i]) { const unsigned char *p=blocks[i];size_t cap=heap_usable_size(p);for (size_t j=0;j<cap;++j) { if (p[j]!=(unsigned char)(i+j+1)) { good=false; } } } }
    if (blocks[0]) {
        void *old=blocks[0];if (kfree((unsigned char *)old+1)) { good=false; }
        void *grown=krealloc(old,200);if (!grown) { good=false; }else { const unsigned char *p=grown;for (unsigned j=0;j<16;++j) { if (p[j]!=(unsigned char)(j+1)) { good=false; } }blocks[0]=grown; }
    }
    for (unsigned i=0;i<10;++i) { if (blocks[i] && !kfree(blocks[i])) { good=false; } }
    struct heap_stats after=heap_get_stats();struct page_stats pafter=pages_stats();
    good=good && heap_check() && before.pages==after.pages && before.objects==after.objects && before.capacity_bytes==after.capacity_bytes && pbefore.free_pages==pafter.free_pages && pbefore.allocated_pages==pafter.allocated_pages;
    console_write(good ? "PASS: heap alignment/zeroing/patterns/realloc/invalid free; live buffers and PMM pages restored.\n" : "FAIL: kernel heap test.\n");
}
bool heap_command(const char *line)
{
    if (same(line,"heapinfo")) { information();return true; }
    if (same(line,"heaptest")) { test();return true; }
    if (same(line,"heapalloc") || prefix(line,"heapalloc ")) {
        unsigned bytes;if (!prefix(line,"heapalloc ") || !decimal(line+10,HEAP_MAX_ALLOCATION,&bytes) || !bytes) { console_write("Usage: heapalloc <1..4096>\n");return true; }
        unsigned slot=0;while (slot<16 && slots[slot]) { ++slot; }
        if (slot==16) { console_write("Heap monitor slots full; use heapfree <slot>.\n");return true; }
        void *p=kmalloc(bytes);if (!p) { console_write("Heap allocation unavailable, exhausted or invalid size.\n");return true; }slots[slot]=p;
        console_write("Heap slot=");console_u64(slot);console_write(" requested=");console_u64(bytes);console_write(" capacity=");console_u64(heap_usable_size(p));console_write(" address=");console_hex((uintptr_t)p);console_write("\n");return true;
    }
    if (same(line,"heapfree") || prefix(line,"heapfree ")) {
        unsigned slot;
        if (!prefix(line,"heapfree ") || !decimal(line+9,15,&slot) || !slots[slot]) { console_write("Invalid or unallocated heap slot.\n");return true; }
        if (!kfree(slots[slot])) { console_write("Heap release rejected.\n");return true; }slots[slot]=NULL;console_write("Freed heap slot=");console_u64(slot);console_write("\n");return true;
    }return false;
}
