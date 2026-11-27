#include "console.h"
#include "cpu.h"
#include "memory.h"
#include <string.h>

enum { SLOT_COUNT=16, MAX_RUN_PAGES=256 };
struct page_slot { uint64_t physical; unsigned count; };
static struct page_slot slots[SLOT_COUNT];

static bool word(const char **cursor,const char *name)
{
    const char *p=*cursor;
    while (*name && *p==*name) { ++p; ++name; }
    if (*name || (*p && *p!=' ')) { return false; }
    while (*p==' ') { ++p; }
    *cursor=p;
    return true;
}

static bool number(const char **cursor,unsigned *out)
{
    const char *p=*cursor;
    if (*p<'0' || *p>'9') { return false; }
    unsigned n=0;
    while (*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p-'0');
        if (n>(UINT32_MAX-digit)/10) { return false; }
        n=n*10+digit;
        ++p;
    }
    if (*p && *p!=' ') { return false; }
    while (*p==' ') { ++p; }
    *cursor=p;
    *out=n;
    return true;
}

static bool zone(const char **p,struct memory_range *range)
{
    const uint64_t split=1ULL<<32;
    if (word(p,"low32")) { *range=(struct memory_range){0,split}; }
    else if (word(p,"high")) { *range=(struct memory_range){split,PMM_ADDRESS_LIMIT-split}; }
    else if (word(p,"all")) { *range=(struct memory_range){0,PMM_ADDRESS_LIMIT}; }
    else { return false; }
    return !**p;
}

static void inventory(unsigned i)
{
    console_write("slot="); console_u64(i);
    console_write(" physical="); console_hex(slots[i].physical);
    console_write(" bytes="); console_u64(slots[i].count*PAGE_SIZE);
    console_write("\n");
}

static void allocate(const char *args,bool single)
{
    unsigned pages=1,alignment=1,boundary=0;
    struct memory_range range={0,PMM_ADDRESS_LIMIT};
    if ((single && *args) || (!single && (!number(&args,&pages) || !pages || pages>MAX_RUN_PAGES ||
        !number(&args,&alignment) || !alignment || (alignment&(alignment-1)) ||
        !number(&args,&boundary) || (boundary && ((boundary&(boundary-1)) || pages>boundary)) ||
        !zone(&args,&range)))) {
        console_write("Usage: pagealloc or pagerun <1..256> <alignment-pages> <boundary-pages|0> <all|low32|high>\n");
        return;
    }
    unsigned i=0;
    while (i<SLOT_COUNT && slots[i].count) { ++i; }
    if (i==SLOT_COUNT) { console_write("Monitor page slots full; use pagefree <slot>.\n"); return; }
    bool ok=single ? page_alloc(&slots[i].physical) :
                     page_alloc_window(pages,alignment,boundary,range,&slots[i].physical);
    if (!ok) { console_write("No suitable physical page run.\n"); return; }
    slots[i].count=pages;
    console_write("Allocated "); inventory(i);
}

static void release_pages(const char *args,bool trim)
{
    unsigned i,keep=0;
    if (!number(&args,&i) || i>=SLOT_COUNT || !slots[i].count) {
        console_write("Invalid or unallocated page slot.\n"); return;
    }
    if ((trim && (!number(&args,&keep) || !keep || keep>=slots[i].count)) || *args) {
        console_write("Expected a slot for pagefree, or slot and smaller positive page count for pagetrim.\n"); return;
    }
    if (!page_free_run(slots[i].physical+keep*PAGE_SIZE,slots[i].count-keep)) {
        console_write("Page release rejected.\n"); return;
    }
    slots[i].count=keep;
    if (keep) { console_write("Trimmed "); inventory(i); }
    else { console_write("Freed slot="); console_u64(i); console_write("\n"); }
}

static void copy_bytes(volatile unsigned char *out, const unsigned char *in)
{
    for (size_t i = 0; i < PAGE_SIZE; ++i) { out[i] = in[i]; }
}

static void fill_bytes(volatile unsigned char *out, unsigned char value)
{
    for (size_t i = 0; i < PAGE_SIZE; ++i) { out[i] = value; }
}

static bool check_page(const unsigned char *page, const unsigned char *source, unsigned char fill)
{
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (page[i] != (source ? source[i] : fill)) { return false; }
    }
    return true;
}

static void benchmark(void)
{
    enum { REPEATS = 128 };
    uint64_t scratch;
    if (!page_alloc_run(2, &scratch)) {
        console_write("membench requires two contiguous scratch pages.\n");
        return;
    }
    unsigned char *source = (void *)(uintptr_t)scratch;
    unsigned char *target = source + PAGE_SIZE;
    for (size_t i = 0; i < PAGE_SIZE; ++i) { source[i] = (unsigned char)(i * 17 + i / 256); }
    memset(target, 0, PAGE_SIZE);

    uint64_t start = timer_ticks();
    for (unsigned i = 0; i < REPEATS; ++i) { copy_bytes(target, source); }
    uint64_t byte_copy = timer_ticks() - start;
    bool valid = check_page(target, source, 0);
    memset(target, 0, PAGE_SIZE);
    start = timer_ticks();
    for (unsigned i = 0; i < REPEATS; ++i) { memcpy(target, source, PAGE_SIZE); }
    uint64_t word_copy = timer_ticks() - start;
    valid &= check_page(target, source, 0);

    start = timer_ticks();
    for (unsigned i = 0; i < REPEATS; ++i) { fill_bytes(target, (unsigned char)i); }
    uint64_t byte_fill = timer_ticks() - start;
    valid &= check_page(target, NULL, REPEATS - 1);
    start = timer_ticks();
    for (unsigned i = 0; i < REPEATS; ++i) { memset(target, (int)i + 128, PAGE_SIZE); }
    uint64_t word_fill = timer_ticks() - start;
    valid &= check_page(target, NULL, 255);
    bool released = page_free_run(scratch, 2);

    console_write("Memory benchmark bytes_per_case="); console_u64(REPEATS * PAGE_SIZE);
    console_write(" counter_hz="); console_u64(timer_frequency()); console_write("\n");
    console_write("copy byte_ticks="); console_u64(byte_copy);
    console_write(" word_ticks="); console_u64(word_copy); console_write("\n");
    console_write("fill byte_ticks="); console_u64(byte_fill);
    console_write(" word_ticks="); console_u64(word_fill); console_write("\n");
    console_write(valid && released ? "PASS: memory copy and fill verified; scratch pages released.\n" :
                                     "FAIL: memory benchmark data or page release.\n");
}

bool page_monitor_command(const char *line)
{
    if (word(&line,"pagerun")) { allocate(line,false); }
    else if (word(&line,"membench")) {
        if (*line) { console_write("Usage: membench\n"); }
        else { benchmark(); }
    }
    else if (word(&line,"pagealloc")) { allocate(line,true); }
    else if (word(&line,"pagetrim")) { release_pages(line,true); }
    else if (word(&line,"pagefree")) { release_pages(line,false); }
    else if (word(&line,"pageslots")) {
        if (*line) { console_write("Usage: pageslots\n"); return true; }
        unsigned count=0;
        for (unsigned i=0;i<SLOT_COUNT;++i) { if (slots[i].count) { inventory(i); ++count; } }
        console_write("Monitor page slots in use="); console_u64(count); console_write("\n");
    } else if (word(&line,"pagezone")) {
        struct memory_range range;
        if (!zone(&line,&range)) { console_write("Usage: pagezone <all|low32|high>\n"); return true; }
        struct page_layout s=page_window_layout(range);
        console_write("managed_pages="); console_u64(s.managed);
        console_write(" free_pages="); console_u64(s.free);
        console_write(" free_runs="); console_u64(s.runs);
        console_write(" largest_run_pages="); console_u64(s.largest); console_write("\n");
    } else { return false; }
    return true;
}
