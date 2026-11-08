/* Bounded reader for the QEMU virt v17 DTB subset, not a general DT library.
 * Supports direct-root PL011, fixed clocks and one contiguous memory bank;
 * deliberately rejects nested bus translations and multiple UARTs/banks. */
#include "board.h"
enum { BEGIN_NODE=1, END_NODE=2, PROP=3, NOP=4, END=9, MAX_DEPTH=32 };
struct node {
    bool uart, memory, fixed_clock, enabled;
    uint64_t address, size;
    uint32_t clock, phandle, frequency;
    bool reg;
};
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static uint64_t be64(const unsigned char *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool contains_string(const unsigned char *p, size_t length, const char *wanted)
{
    size_t at = 0;
    while (at < length) {
        size_t start = at;
        while (at < length && p[at]) { ++at; }
        if (at == length) { return false; }
        if (equal((const char *)p + start, wanted)) { return true; }
        ++at;
    }
    return false;
}
static bool region(uint32_t offset, uint32_t size, uint32_t total)
{
    return offset >= 40 && offset <= total && size <= total - offset;
}
bool fdt_read_board(const void *blob, size_t available, struct board_info *out)
{
    if (!blob || !out || available < 40) { return false; }
    const unsigned char *data = blob;
    if (be32(data) != 0xd00dfeed) { return false; }
    uint32_t total=be32(data+4), so=be32(data+8), no=be32(data+12);
    uint32_t ns=be32(data+32), ss=be32(data+36);
    if (total > available || be32(data+20) < 17 || be32(data+24) > 17 ||
        (so & 3) || !region(so, ss, total) || !region(no, ns, total) ||
        (so < no + ns && no < so + ss)) { return false; }
    const unsigned char *p=data+so, *end=p+ss;
    struct node nodes[MAX_DEPTH] = {0};
    struct { uint32_t phandle, frequency; } clocks[8] = {0};
    struct board_info result = {0};
    unsigned depth=0, clock_count=0, uart_count=0, memory_count=0;
    uint32_t address_cells=0, size_cells=0, uart_clock=0;
    bool saw_root=false;
    while ((size_t)(end-p) >= 4) {
        uint32_t token=be32(p); p+=4;
        if (token == BEGIN_NODE) {
            if (depth == MAX_DEPTH || (!depth && saw_root)) { return false; }
            if (!depth) { if (p == end || *p) { return false; } saw_root=true; }
            while (p < end && *p) { ++p; }
            if (p == end) { return false; }
            ++p;
            size_t pad=(4-((size_t)(p-data)&3))&3;
            if ((size_t)(end-p)<pad) { return false; }
            p+=pad;
            nodes[depth++] = (struct node){.enabled=true};
        } else if (token == END_NODE) {
            if (!depth) { return false; }
            struct node *n=&nodes[depth-1];
            if (depth == 2 && n->enabled) {
                if (n->uart || n->memory) {
                    if (!n->reg || address_cells != 2 || size_cells != 2 ||
                        !n->size || n->address > UINT64_MAX-n->size) { return false; }
                }
                if (n->uart) {
                    if (++uart_count > 1 || n->size < 0x1000) { return false; }
                    result.uart_base=(uintptr_t)n->address; uart_clock=n->clock;
                }
                if (n->memory) {
                    if (++memory_count > 1) { return false; }
                    result.ram_base=n->address; result.ram_size=n->size;
                }
                if (n->fixed_clock) {
                    if (clock_count == 8 || !n->phandle || !n->frequency) { return false; }
                    clocks[clock_count].phandle=n->phandle;
                    clocks[clock_count++].frequency=n->frequency;
                }
            }
            --depth;
        } else if (token == PROP) {
            if (!depth || (size_t)(end-p)<8) { return false; }
            uint32_t length=be32(p), nameoff=be32(p+4); p+=8;
            if (nameoff >= ns || length > (size_t)(end-p)) { return false; }
            size_t nameend=nameoff;
            while (nameend < ns && data[no+nameend]) { ++nameend; }
            if (nameend == ns) { return false; }
            const char *name=(const char *)data+no+nameoff;
            struct node *n=&nodes[depth-1];
            if (depth == 1 && length == 4) {
                if (equal(name,"#address-cells")) { address_cells=be32(p); }
                if (equal(name,"#size-cells")) { size_cells=be32(p); }
            }
            if (depth == 2) {
                if (equal(name,"compatible")) {
                    n->uart=contains_string(p,length,"arm,pl011");
                    n->fixed_clock=contains_string(p,length,"fixed-clock");
                } else if (equal(name,"device_type")) {
                    n->memory=contains_string(p,length,"memory");
                } else if (equal(name,"status")) {
                    n->enabled=contains_string(p,length,"okay") || contains_string(p,length,"ok");
                } else if (equal(name,"reg") && length == 16) {
                    n->address=be64(p); n->size=be64(p+8); n->reg=true;
                } else if (equal(name,"clocks") && length >= 4) {
                    n->clock=be32(p);
                } else if (equal(name,"phandle") && length == 4) {
                    n->phandle=be32(p);
                } else if (equal(name,"clock-frequency") && length == 4) {
                    n->frequency=be32(p);
                }
            }
            p+=length;
            size_t pad=(4-(length&3))&3;
            if ((size_t)(end-p)<pad) { return false; }
            p+=pad;
        } else if (token == END) {
            if (depth || !saw_root || uart_count != 1 || memory_count != 1) { return false; }
            for (unsigned i=0; i<clock_count; ++i) {
                if (clocks[i].phandle == uart_clock) {
                    if (result.uart_clock) { return false; }
                    result.uart_clock=clocks[i].frequency;
                }
            }
            if (!result.uart_clock) { return false; }
            *out=result;
            return true;
        } else if (token != NOP) { return false; }
    }
    return false;
}
