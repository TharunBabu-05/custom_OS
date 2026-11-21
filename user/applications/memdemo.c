#include "user.h"
static void number(uint64_t n)
{ char b[24];unsigned i=0;do { b[i++]=(char)('0'+n%10);n/=10; }while (n);while (i) { uos_write(b+--i,1); } }
static int pressure_report(void)
{
    struct uos_memory_pressure info;
    if (uos_memory_pressure(&info)) {
        uos_puts("Memory pressure report unavailable on this kernel.\n");return 1;
    }
    uos_puts("Private heap free_pages=");number(info.free_pages);
    uos_puts(" largest_run=");number(info.largest_run);uos_puts("\n");
    uos_puts("quota_failures=");number(info.quota_failures);
    uos_puts(" fragmentation_failures=");number(info.fragmentation_failures);
    uos_puts(" backing_failures=");number(info.backing_failures);
    uos_puts(" mapping_failures=");number(info.mapping_failures);uos_puts("\n");
    return 0;
}
int app_main(void)
{
    if (uos_argc()>1) {
        char mode[sizeof("pressure")];
        const char option[]="pressure";
        if (uos_argc()!=2 || uos_arg(1,mode,sizeof(mode))!=(int64_t)(sizeof(option)-1)) { return 1; }
        for (unsigned i=0;i<sizeof(option)-1;++i) { if (mode[i]!=option[i]) { return 1; } }
        return pressure_report();
    }
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
