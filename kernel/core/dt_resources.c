#include "dt_resources.h"

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return *a==*b; }

static bool copy_path(char out[DT_PATH_CAPACITY],const char *path)
{
    for (unsigned i=0;i<DT_PATH_CAPACITY;++i) {
        if (!(out[i]=path[i])) { return true; }
    }
    return false;
}

bool dt_parent_path(char *path)
{
    if (path[0]!='/' || !path[1]) { return false; }
    char *last=path;
    for (char *p=path+1; *p; ++p) {
        if (*p=='/') { last=p; }
    }
    last[last==path]='\0';
    return true;
}

enum dt_state dt_local_state(const struct dt_node *node)
{
    const unsigned char *p;
    uint32_t bytes;
    enum dt_lookup result=dt_node_property(node,"status",&p,&bytes);
    if (result==DT_MISSING) { return DT_ENABLED; }
    if (result!=DT_FOUND || !bytes || p[bytes-1] || !*p) { return DT_BAD_STATUS; }
    for (uint32_t i=0;i+1<bytes;++i) {
        if (p[i]<32 || p[i]>126) { return DT_BAD_STATUS; }
    }
    return same((const char *)p,"okay") || same((const char *)p,"ok") ? DT_ENABLED : DT_DISABLED;
}

enum dt_state dt_device_state(const void *dtb,size_t bytes,const char *path)
{
    char parent[DT_PATH_CAPACITY];
    if (!copy_path(parent,path)) { return DT_BAD_STATUS; }
    enum dt_state state=DT_ENABLED;
    do {
        struct dt_node node;
        if (dt_find_node(dtb,bytes,parent,&node)!=DT_FOUND) { return DT_BAD_STATUS; }
        enum dt_state local=dt_local_state(&node);
        if (local==DT_BAD_STATUS) { return local; }
        if (local==DT_DISABLED) { state=local; }
    } while (dt_parent_path(parent));
    return state;
}

struct bus { unsigned addr,size; bool pci; };
struct address { uint64_t value; unsigned space; };

static unsigned cells(const struct dt_node *node,const char *key,unsigned fallback)
{
    const unsigned char *p;
    uint32_t bytes;
    enum dt_lookup result=dt_node_property(node,key,&p,&bytes);
    if (result==DT_MISSING) { return fallback; }
    return result==DT_FOUND && bytes==4 ? dt_cell(p) : UINT32_MAX;
}

static bool bus_layout(const struct dt_node *node,struct bus *out)
{
    const unsigned char *p;
    uint32_t bytes;
    bool pci=dt_node_property(node,"device_type",&p,&bytes)==DT_FOUND && bytes==4 &&
        p[0]=='p' && p[1]=='c' && p[2]=='i' && !p[3];
    unsigned addr=cells(node,"#address-cells",2),size=cells(node,"#size-cells",1);
    if (!addr || addr>(pci ? 3u : 2u) || size>2 || (pci && addr!=3)) { return false; }
    *out=(struct bus){addr,size,pci};
    return true;
}

static uint64_t read_cells(const unsigned char *p,unsigned count)
{
    uint64_t value=0;
    for (unsigned i=0;i<count;++i) { value=value<<32 | dt_cell(p+4*i); }
    return value;
}

static struct address read_address(const unsigned char *p,struct bus bus)
{
    if (!bus.pci) { return (struct address){read_cells(p,bus.addr),0}; }
    return (struct address){read_cells(p+4,2),(dt_cell(p)>>24)&3};
}

static bool fits(uint64_t address,uint64_t size,struct bus bus)
{
    uint64_t max=bus.addr==1 ? UINT32_MAX : UINT64_MAX;
    return size && address<=max && size-1<=max-address;
}

static bool translate(const struct dt_node *node,struct bus child,struct bus parent,
                      uint64_t size,struct address *address)
{
    const unsigned char *p;
    uint32_t bytes;
    if (dt_node_property(node,"ranges",&p,&bytes)!=DT_FOUND) { return false; }
    if (!bytes) { return child.pci==parent.pci && fits(address->value,size,parent); }
    unsigned stride=4*(child.addr+parent.addr+child.size);
    if (!child.size || bytes%stride) { return false; }

    struct address translated={0};
    bool found=false;
    for (unsigned offset=0;offset<bytes;offset+=stride) {
        const unsigned char *entry=p+offset;
        struct address start=read_address(entry,child);
        struct address target=read_address(entry+4*child.addr,parent);
        uint64_t span=read_cells(entry+4*(child.addr+parent.addr),child.size);
        if (!fits(start.value,span,child) || !fits(target.value,span,parent)) { return false; }
        if (start.space!=address->space || address->value<start.value || size>span) { continue; }
        uint64_t delta=address->value-start.value;
        if (delta>span-size) { continue; }
        if (found) { return false; }
        target.value+=delta;
        translated=target;
        found=true;
    }
    if (found) { *address=translated; }
    return found;
}

enum dt_reg_result dt_reg_range(const void *dtb,size_t bytes,const char *path,const struct dt_node *device,
                               unsigned index,struct dt_reg_range *out)
{
    const unsigned char *reg;
    uint32_t length;
    enum dt_lookup result=dt_node_property(device,"reg",&reg,&length);
    if (result==DT_MISSING) { return DT_REG_END; }
    if (result!=DT_FOUND) { return DT_REG_RAW; }
    char parent[DT_PATH_CAPACITY];
    if (!copy_path(parent,path)) { return DT_REG_RAW; }
    struct dt_node bus_node;
    struct bus bus;
    if (!dt_parent_path(parent) || dt_find_node(dtb,bytes,parent,&bus_node)!=DT_FOUND ||
        !bus_layout(&bus_node,&bus) || !bus.size) { return DT_REG_RAW; }
    unsigned stride=4*(bus.addr+bus.size);
    if (length%stride) { return DT_REG_RAW; }
    if (index>=length/stride) { return DT_REG_END; }
    reg+=stride*index;
    struct address address=read_address(reg,bus);
    uint64_t size=read_cells(reg+4*bus.addr,bus.size);
    *out=(struct dt_reg_range){.address=address.value,.bytes=size};
    if (!fits(address.value,size,bus)) { return DT_REG_LOCAL; }
    while (dt_parent_path(parent)) {
        struct dt_node upstream;
        struct bus layout;
        if (dt_find_node(dtb,bytes,parent,&upstream)!=DT_FOUND || !bus_layout(&upstream,&layout) ||
            !translate(&bus_node,bus,layout,size,&address)) { return DT_REG_LOCAL; }
        bus_node=upstream;
        bus=layout;
    }
    if (address.space) { return DT_REG_LOCAL; }
    out->cpu=address.value;
    return DT_REG_CPU;
}
