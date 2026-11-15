#include "user.h"
#include "allocator.h"
static void number(uint64_t n)
{ char text[24];unsigned i=0;do { text[i++]=(char)('0'+n%10);n/=10; }while (n);while (i) { uos_write(text+--i,1); } }
static void report(const char *label,struct uos_allocator *heap)
{
    struct uos_allocator_stats s=uos_allocator_stats(heap);uos_puts(label);number(s.requested_bytes);uos_puts(" bytes; reserved_pages=");number(s.pages);uos_puts("\n");
}
int app_main(void)
{
    struct uos_allocator heap={0};if (!uos_allocator_init(&heap)) { return 1; }unsigned char *objects[32];
    for (unsigned i=0;i<32;++i) {
        objects[i]=uos_malloc(&heap,24);if (!objects[i]) { return 2; }
        for (unsigned j=0;j<24;++j) { if (objects[i][j]) { return 3; }objects[i][j]=42; }
    }
    report("Small buffers: requested=",&heap);
    unsigned char *grown=uos_realloc(&heap,objects[0],5000);if (!grown) { return 4; }objects[0]=grown;
    for (unsigned i=0;i<5000;++i) { if (grown[i]!=(i<24 ? 42 : 0)) { return 5; } }
    report("After resize: requested=",&heap);
    unsigned char *array=uos_calloc(&heap,16,16);if (!array) { return 6; }
    for (unsigned i=0;i<256;++i) { if (array[i]) { return 7; } }report("After calloc: requested=",&heap);
    for (unsigned i=0;i<32;++i) { if (!uos_free(&heap,objects[i])) { return 8; } }
    if (!uos_free(&heap,array) || !uos_allocator_destroy(&heap)) { return 9; }
    struct uos_memory_info info;if (uos_memory_info(&info) || info.live_pages) { return 10; }
    uos_puts("Released buffers; private_pages=");number(info.live_pages);uos_puts("\n");return 0;
}
