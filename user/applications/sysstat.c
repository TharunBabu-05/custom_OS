#include "text_io.h"

enum { WATCH_MAX=10, WATCH_DELAY_MS=250 };

static bool same(const char *left,const char *right)
{
    while (*left && *left==*right) { ++left; ++right; }
    return *left==*right;
}

static bool print(const char *text)
{
    return text_write(text,uos_length(text));
}

static bool metric(const char *label,uint64_t value)
{
    char digits[20];
    size_t start=sizeof(digits);
    do {
        digits[--start]=(char)('0'+value%10);
        value/=10;
    } while (value);
    return print(label) && text_write(digits+start,sizeof(digits)-start);
}

static bool report(const struct uos_system_stats *s)
{
    return metric("uptime_ms=",s->uptime_ms) &&
        print(s->flags&UOS_SYSTEM_TIMER ? " timer=on" : " timer=off") &&
        metric(" delivered=",s->timer_delivered) && metric(" skipped=",s->timer_skipped) &&
        metric(" unexpected_irqs=",s->unexpected_irqs) && print("\n") &&
        metric("workers_ready=",s->workers_ready) && metric(" parked=",s->workers_parked) &&
        metric(" faulted=",s->workers_faulted) && metric(" transitioning=",s->workers_transitioning) && print("\n") &&
        print(s->flags&UOS_SYSTEM_PMM ? "pmm=ready" : "pmm=unavailable") &&
        metric(" ram_bytes=",s->ram_bytes) && metric(" managed_pages=",s->managed_pages) &&
        metric(" free_pages=",s->free_pages) && metric(" allocated_pages=",s->allocated_pages) &&
        metric(" peak_pages=",s->peak_pages) && metric(" allocation_failures=",s->allocation_failures) && print("\n") &&
        print(s->flags&UOS_SYSTEM_HEAP_FAILED ? "heap=failed" : s->flags&UOS_SYSTEM_HEAP ? "heap=ready" : "heap=unavailable") &&
        metric(" pages=",s->heap_pages) && metric(" objects=",s->heap_objects) &&
        metric(" capacity_bytes=",s->heap_bytes) && metric(" failures=",s->heap_failures) && print("\n") &&
        metric("foreground=",s->foreground) && metric("/",s->foreground_limit) &&
        metric(" services=",s->services) && metric("/",s->service_limit) &&
        metric(" runnable=",s->runnable) && metric(" sleeping=",s->sleeping) &&
        metric(" blocked=",s->blocked) && print("\n") &&
        metric("files=",s->files) && metric("/",s->file_limit) &&
        metric(" claims=",s->claims) && metric("/",s->claim_limit) &&
        print(s->flags&UOS_SYSTEM_BOOT ? " boot=mounted" : " boot=unmounted") &&
        print(s->flags&UOS_SYSTEM_DATA_FAILED ? " data=failed\n" :
              s->flags&UOS_SYSTEM_DATA ? " data=mounted\n" : " data=unmounted\n");
}

static bool read_stats(struct uos_system_stats *stats)
{
    if (!uos_system_stats(stats) && stats->version==UOS_SYSTEM_STATS_VERSION) { return true; }
    print("sysstat: system statistics unavailable\n");
    return false;
}

static int watch(unsigned count)
{
    struct uos_system_stats first,next;
    if (!read_stats(&first)) { return 1; }
    for (unsigned sample=1;sample<=count;++sample) {
        if (uos_sleep_ms(WATCH_DELAY_MS)<0 || !read_stats(&next)) { return 1; }
        if (!metric("sample=",sample) ||
            !metric(" elapsed_ms=",next.uptime_ms-first.uptime_ms) ||
            !metric(" timer_delta=",next.timer_delivered-first.timer_delivered) ||
            !metric(" skipped_delta=",next.timer_skipped-first.timer_skipped) ||
            !metric(" free_pages=",next.free_pages) || !metric(" runnable=",next.runnable) ||
            !metric(" sleeping=",next.sleeping) || !metric(" blocked=",next.blocked) || !print("\n")) {
            return 1;
        }
        first=next;
    }
    return 0;
}

static int check(void)
{
    struct uos_system_stats s={.version=UINT32_MAX,.reserved=UINT32_MAX};
    int64_t bad_size=uos_call2(UOS_SYS_SYSTEM_STATS,(uintptr_t)&s,sizeof(s)-1);
    if (bad_size!=UOS_ERROR || s.version!=UINT32_MAX || s.reserved!=UINT32_MAX ||
        uos_system_stats(NULL)!=UOS_EFAULT || !read_stats(&s) || s.reserved ||
        s.free_pages>s.managed_pages || s.allocated_pages!=s.managed_pages-s.free_pages ||
        s.allocated_pages>s.peak_pages || !s.foreground_limit || !s.service_limit ||
        s.foreground>s.foreground_limit || s.services>s.service_limit ||
        s.runnable+s.sleeping+s.blocked!=s.foreground+s.services ||
        s.files>s.file_limit || s.claims>s.claim_limit) {
        print("FAIL: system statistics ABI or counters\n");
        return 1;
    }
    return print("PASS: system statistics ABI, destination checks and accounting.\n") ? 0 : 1;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char mode[16];
    if (argc==1) {
        struct uos_system_stats stats;
        return read_stats(&stats) && report(&stats) ? 0 : 1;
    }
    if (argc>=2 && uos_arg(1,mode,sizeof(mode))>0) {
        if (argc==2 && same(mode,"check")) { return check(); }
        if (argc==3 && same(mode,"watch")) {
            char arg[8]; unsigned count;
            if (uos_arg(2,arg,sizeof(arg))>0 && text_count(arg,WATCH_MAX,&count) && count) { return watch(count); }
        }
    }
    print("Usage: sysstat [check | watch <1..10>]\n");
    return 2;
}
