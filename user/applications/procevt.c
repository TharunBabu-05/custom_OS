#include "text_io.h"
#include "process_event_names.h"

static bool same(const char *left,const char *right)
{
    while (*left && *left==*right) { ++left; ++right; }
    return *left==*right;
}

static bool parse_cursor(const char *text,uint64_t *out)
{
    if (!*text) { return false; }
    uint64_t n=0;
    for (;*text;++text) {
        if (*text<'0' || *text>'9') { return false; }
        unsigned digit=(unsigned)(*text-'0');
        if (n>(UINT64_MAX-digit)/10) { return false; }
        n=n*10+digit;
    }
    *out=n;
    return true;
}

static bool print(const char *text) { return text_write(text,uos_length(text)); }

static bool field(const char *label,uint64_t value)
{
    char digits[20];
    size_t start=sizeof(digits);
    do {
        digits[--start]=(char)('0'+value%10);
        value/=10;
    } while (value);
    return print(label) && text_write(digits+start,sizeof(digits)-start);
}

static bool print_event(const struct uos_process_event *e)
{
    if (!field("seq=",e->sequence) || !field(" ms=",e->uptime_ms) ||
        !print(" ") || !print(process_event_name(e->event)) ||
        !print(" ") || !print(process_role_name(e->role)) ||
        !field(" pid=",e->pid) || !field(" ppid=",e->ppid) ||
        !print(" reason=") || !print(process_stop_name(e->reason)) ||
        !field(" code=",e->code) || !field(" signal=",e->signal)) { return false; }
    if (e->event==UOS_PROCESS_EVENT_REAP && !print(e->flags ? " cleanup=failed" : " cleanup=ok")) {
        return false;
    }
    return print("\n");
}

static bool read_page(uint64_t cursor,struct uos_process_events *out)
{
    if (!uos_process_events(cursor,out) && out->version==UOS_PROCESS_EVENT_VERSION &&
        out->count<=UOS_PROCESS_EVENT_ROWS && out->cursor>=cursor && out->cursor<=out->latest &&
        (out->cursor!=cursor || cursor==out->latest)) { return true; }
    print("procevt: history unavailable or cursor ahead of this boot\n");
    return false;
}

static bool report(uint64_t *cursor)
{
    struct uos_process_events page;
    if (!read_page(*cursor,&page)) { return false; }
    /* Fix the upper bound on the first read so concurrent events can wait
     * for the next sample. */
    uint64_t end=page.latest;
    uint64_t from=*cursor;
    for (;;) {
        uint64_t expired=page.expired>end-from ? end-from : page.expired;
        if (expired && (!field("expired=",expired) || !print("\n"))) { return false; }
        for (unsigned i=0;i<page.count && page.rows[i].sequence<=end;++i) {
            if (!print_event(&page.rows[i])) { return false; }
        }
        if (page.cursor>=end) { break; }
        from=page.cursor;
        if (!read_page(from,&page)) { return false; }
    }
    *cursor=end;
    return field("cursor=",end) && print("\n");
}

static int watch(unsigned count)
{
    struct uos_process_events page;
    if (!read_page(0,&page)) { return 1; }
    uint64_t cursor=page.latest;
    for (unsigned i=1;i<=count;++i) {
        if (uos_sleep_ms(250) || !field("sample=",i) || !print("\n") || !report(&cursor)) { return 1; }
    }
    return 0;
}

static int check(void)
{
    struct uos_process_events page={.version=UINT32_MAX};
    if (uos_call4(UOS_SYS_PROCESS_EVENTS,(uintptr_t)&page,sizeof(page)-1,0,0)!=UOS_ERROR ||
        uos_process_events(UINT64_MAX,&page)!=UOS_ERROR || page.version!=UINT32_MAX ||
        uos_process_events(0,NULL)!=UOS_EFAULT || !read_page(0,&page) ||
        !read_page(page.latest,&page) || page.expired) {
        print("FAIL: process event ABI or cursor checks\n");
        return 1;
    }
    return print("PASS: process event ABI, checked destination and cursor.\n") ? 0 : 1;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    uint64_t cursor=0;
    if (argc==1) { return report(&cursor) ? 0 : 1; }
    char mode[16],value[24];
    if (argc>=2 && uos_arg(1,mode,sizeof(mode))>0) {
        if (argc==2 && same(mode,"check")) { return check(); }
        if (argc==3 && uos_arg(2,value,sizeof(value))>0) {
            unsigned count;
            if (same(mode,"since") && parse_cursor(value,&cursor)) { return report(&cursor) ? 0 : 1; }
            if (same(mode,"watch") && text_count(value,8,&count) && count) { return watch(count); }
        }
    }
    print("Usage: procevt [since SEQUENCE | watch <1..8> | check]\n");
    return 2;
}
