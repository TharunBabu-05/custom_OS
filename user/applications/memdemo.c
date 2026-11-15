#include "user.h"
static void number(uint64_t n)
{ char b[24];unsigned i=0;do { b[i++]=(char)('0'+n%10);n/=10; }while (n);while (i) { uos_write(b+--i,1); } }
int app_main(void)
{
    struct uos_memory_info info;if (uos_memory_info(&info)) { return 1; }
    uos_puts("Application page arena: ");number(info.limit_pages*info.page_bytes/1024);uos_puts(" KiB; live pages=");number(info.live_pages);uos_puts("\n");
    int64_t address=uos_alloc_pages(4);if (address<0) { uos_puts("Allocation failed.\n");return 2; }
    unsigned char *p=(void *)(uintptr_t)address;uint64_t sum=0;
    for (unsigned i=0;i<16384;++i) { if (p[i]) { return 3; }p[i]=(unsigned char)(i*33+17);sum+=p[i]; }
    if (uos_memory_info(&info) || info.live_pages!=4) { return 4; }
    uos_puts("Working buffer: 16 KiB; live pages=");number(info.live_pages);uos_puts("; checksum=");number(sum);uos_puts("\n");
    if (uos_free_pages(p) || uos_memory_info(&info) || info.live_pages) { return 5; }
    uos_puts("Released buffer; live pages=");number(info.live_pages);uos_puts("\n");return 0;
}
