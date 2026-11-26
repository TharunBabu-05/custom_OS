#include "console.h"
#include "memory.h"

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

bool page_monitor_command(const char *line)
{
    if (word(&line,"pagerun")) { allocate(line,false); }
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
