/* DTSpec v17. Root memory tuples, memreserve, reserved-memory and initrd.
 * Dynamic requests conservatively exclude their entire alloc-ranges windows;
 * requests without a bounded window and translations fail closed. */
#include "memory.h"
enum { BEGIN_NODE=1, END_NODE=2, PROP=3, NOP=4, END=9, MAX_DEPTH=32 };
struct node {
    bool enabled, parent_enabled, children, memory_name, memory_type, reserved, chosen, dynamic;
    unsigned ac, sc;
    const unsigned char *reg;
    uint32_t reg_size;
    const unsigned char *alloc_ranges;
    uint32_t alloc_size;
    bool has_reg, ranges, ac_seen, sc_seen, type_seen, status_seen, size_seen;
};
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32 | be32(p+4); }
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static bool text(const unsigned char *p, uint32_t length, const char *value)
{
    uint32_t i=0;
    while (i<length && value[i] && p[i] == (unsigned char)value[i]) { ++i; }
    return i<length && i+1 == length && !p[i] && !value[i];
}
static bool region(uint32_t offset, uint32_t length, uint32_t total)
{
    return offset >= 40 && offset <= total && length <= total-offset;
}
static bool overlaps(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return a < b+bn && b < a+an;
}
static uint64_t cells(const unsigned char *p, unsigned count) { return count == 2 ? be64(p) : be32(p); }
static bool tuples(struct memory_map *map, const struct node *n, unsigned ac, unsigned sc, const char *reason)
{
    if ((ac != 1 && ac != 2) || (sc != 1 && sc != 2) || !n->has_reg ||
        !n->reg_size || n->reg_size % ((ac+sc)*4)) { return false; }
    for (uint32_t at=0; at<n->reg_size; at+=(ac+sc)*4) {
        uint64_t base=cells(n->reg+at,ac), size=cells(n->reg+at+ac*4,sc);
        /* Zero-sized reg entries are valid placeholders in firmware DTs. */
        if (!size) { continue; }
        if (base > UINT64_MAX-size) { return false; }
        if (reason) {
            if (!memory_reserve(map,base,size,reason)) { return false; }
        } else {
            if (map->ram_count == MEMORY_MAX_RAM) { return false; }
            map->ram[map->ram_count++]=(struct memory_range){base,size};
        }
    }
    return true;
}
bool fdt_read_memory(const void *blob, size_t available, struct memory_map *out)
{
    if (!blob || !out || available < 40) { return false; }
    const unsigned char *data=blob;
    if (be32(data) != 0xd00dfeed) { return false; }
    uint32_t total=be32(data+4), so=be32(data+8), no=be32(data+12), ro=be32(data+16);
    uint32_t ns=be32(data+32), ss=be32(data+36);
    if (total > available || be32(data+20) < 17 || be32(data+24) > 17 ||
        (so&3) || (ro&7) || !region(so,ss,total) || !region(no,ns,total) ||
        !region(ro,16,total) || overlaps(so,ss,no,ns)) { return false; }
    struct memory_map result={0}; result.dtb_size=total;
    uint32_t rp=ro;
    for (;;) {
        if (!region(rp,16,total) || overlaps(rp,16,so,ss) || overlaps(rp,16,no,ns)) { return false; }
        uint64_t base=be64(data+rp), size=be64(data+rp+8);
        rp+=16;
        if (!base && !size) { break; }
        if (!memory_reserve(&result,base,size,"memreserve")) { return false; }
    }
    const unsigned char *p=data+so, *end=p+ss;
    struct node nodes[MAX_DEPTH]={0}; unsigned depth=0;
    bool saw_root=false, closed_root=false, saw_reserved=false;
    bool initrd_start_seen=false, initrd_end_seen=false;
    uint64_t initrd_start=0, initrd_end=0;
    while ((size_t)(end-p) >= 4) {
        uint32_t token=be32(p); p+=4;
        if (token == BEGIN_NODE) {
            if (depth == MAX_DEPTH || closed_root || (!depth && saw_root)) { return false; }
            const unsigned char *name=p;
            while (p<end && *p) { ++p; }
            if (p == end) { return false; }
            uint32_t name_size=(uint32_t)(p-name);
            if (!depth && name_size) { return false; }
            ++p;
            size_t pad=(4-((size_t)(p-data)&3))&3;
            if ((size_t)(end-p)<pad) { return false; } p+=pad;
            struct node n={.enabled=true,.parent_enabled=true};
            if (depth) {
                nodes[depth-1].children=true;
                n.parent_enabled=nodes[depth-1].parent_enabled && nodes[depth-1].enabled;
            } else { saw_root=true; }
            if (depth == 1) {
                n.memory_name=(name_size == 6 && same((const char *)name,"memory")) ||
                    (name_size > 7 && name[0]=='m' && name[1]=='e' && name[2]=='m' &&
                     name[3]=='o' && name[4]=='r' && name[5]=='y' && name[6]=='@');
                n.reserved=same((const char *)name,"reserved-memory");
                n.chosen=same((const char *)name,"chosen");
                if (n.reserved) { if (saw_reserved) { return false; } saw_reserved=true; }
            }
            nodes[depth++]=n;
        } else if (token == PROP) {
            if (!depth || nodes[depth-1].children || (size_t)(end-p)<8) { return false; }
            uint32_t length=be32(p), nameoff=be32(p+4); p+=8;
            if (length > (size_t)(end-p) || nameoff >= ns) { return false; }
            uint32_t nameend=nameoff;
            while (nameend<ns && data[no+nameend]) { ++nameend; }
            if (nameend == ns) { return false; }
            const char *name=(const char *)data+no+nameoff;
            struct node *n=&nodes[depth-1];
            if ((depth == 1 || n->reserved) && same(name,"#address-cells")) {
                if (length != 4 || n->ac_seen) { return false; } n->ac=be32(p); n->ac_seen=true;
            } else if ((depth == 1 || n->reserved) && same(name,"#size-cells")) {
                if (length != 4 || n->sc_seen) { return false; } n->sc=be32(p); n->sc_seen=true;
            } else if (same(name,"status")) {
                if (!length || p[length-1] || n->status_seen) { return false; }
                n->status_seen=true; n->enabled=text(p,length,"okay") || text(p,length,"ok");
            } else if (depth == 2 && same(name,"device_type")) {
                if (n->type_seen) { return false; } n->type_seen=true; n->memory_type=text(p,length,"memory");
            } else if ((depth == 2 || (depth == 3 && nodes[1].reserved)) && same(name,"reg")) {
                if (n->has_reg) { return false; } n->has_reg=true; n->reg=p; n->reg_size=length;
            } else if (n->reserved && same(name,"ranges")) {
                if (n->ranges || length) { return false; } n->ranges=true;
            } else if (depth == 3 && nodes[1].reserved && same(name,"size")) {
                if ((length != 4 && length != 8) || n->size_seen) { return false; }
                n->size_seen=true;
                n->dynamic=cells(p,length/4) != 0;
            } else if (depth == 3 && nodes[1].reserved && same(name,"alloc-ranges")) {
                if (n->alloc_ranges) { return false; }
                n->alloc_ranges=p; n->alloc_size=length;
            } else if ((n->chosen && same(name,"linux,usable-memory-range")) ||
                       (depth == 2 && same(name,"linux,usable-memory"))) { return false;
            } else if (n->chosen && same(name,"linux,initrd-start")) {
                if ((length != 4 && length != 8) || initrd_start_seen) { return false; }
                initrd_start_seen=true; initrd_start=cells(p,length/4);
            } else if (n->chosen && same(name,"linux,initrd-end")) {
                if ((length != 4 && length != 8) || initrd_end_seen) { return false; }
                initrd_end_seen=true; initrd_end=cells(p,length/4);
            }
            p+=length;
            size_t pad=(4-(length&3))&3;
            if ((size_t)(end-p)<pad) { return false; } p+=pad;
        } else if (token == END_NODE) {
            if (!depth) { return false; }
            struct node *n=&nodes[depth-1];
            if (n->enabled && n->parent_enabled) {
                if (depth == 2 && (n->memory_name || n->memory_type)) {
                    if (!n->memory_type || !tuples(&result,n,nodes[0].ac,nodes[0].sc,NULL)) { return false; }
                } else if (depth == 3 && nodes[1].reserved) {
                    if (n->children) { return false; }
                    if (n->has_reg) {
                        if (!tuples(&result,n,nodes[1].ac,nodes[1].sc,"reserved-memory")) { return false; }
                    } else if (n->dynamic) {
                        struct node window=*n;
                        window.reg=n->alloc_ranges; window.reg_size=n->alloc_size; window.has_reg=true;
                        unsigned before=result.reserved_count;
                        if (!n->alloc_ranges || !tuples(&result,&window,nodes[1].ac,nodes[1].sc,
                            "dynamic reservation window (conservative)") || result.reserved_count == before) { return false; }
                    }
                } else if (depth == 2 && n->reserved) {
                    if (!n->ranges || !n->ac_seen || !n->sc_seen ||
                        n->ac != nodes[0].ac || n->sc != nodes[0].sc) { return false; }
                }
            }
            --depth;
            if (!depth) { closed_root=true; }
        } else if (token == END) {
            if (depth || !saw_root || !closed_root || !nodes[0].ac_seen || !nodes[0].sc_seen ||
                (nodes[0].ac != 1 && nodes[0].ac != 2) || (nodes[0].sc != 1 && nodes[0].sc != 2) ||
                initrd_start_seen != initrd_end_seen) { return false; }
            if (initrd_start_seen && (initrd_end <= initrd_start ||
                !memory_reserve(&result,initrd_start,initrd_end-initrd_start,"initrd"))) { return false; }
            if (!memory_normalize(&result)) { return false; }
            *out=result; return true;
        } else if (token != NOP) { return false; }
    }
    return false;
}
