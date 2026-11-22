#include "board.h"
#include "console.h"
#include "dt_resources.h"

enum { DT_READ_LIMIT=0x200000, PRINT_CELLS=32, PRINT_RANGES=16 };

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return *a==*b; }

static bool single_path(const unsigned char *p,uint32_t bytes)
{
    if (bytes<2 || bytes>DT_PATH_CAPACITY || p[0]!='/' || p[bytes-1]) { return false; }
    for (uint32_t i=1;i+1<bytes;++i) {
        if (p[i]<=32 || p[i]>126) { return false; }
    }
    return true;
}

static const char *resolve(const void *dtb,const char *name)
{
    const unsigned char *p;
    uint32_t bytes;
    if (*name=='/') { return name; }
    if (dt_property(dtb,DT_READ_LIMIT,"/aliases",name,&p,&bytes) && single_path(p,bytes)) {
        return (const char *)p;
    }
    return NULL;
}

static void show_status(enum dt_state state)
{ console_write(state==DT_ENABLED ? "enabled" : state==DT_DISABLED ? "disabled" : "invalid"); }

static void show_compatible(const struct dt_node *node,bool first_only)
{
    const unsigned char *p;
    uint32_t bytes;
    enum dt_lookup result=dt_node_property(node,"compatible",&p,&bytes);
    if (result==DT_MISSING) { return; }
    console_write(" compatible=");
    if (result!=DT_FOUND || !bytes || p[bytes-1]) { console_write("invalid"); return; }
    for (uint32_t i=0;i+1<bytes && i<160;++i) {
        if (!p[i] && first_only) { return; }
        console_putc(!p[i] ? ',' : p[i]>=32 && p[i]<=126 ? (char)p[i] : '?');
    }
    if (bytes>161) { console_write("..."); }
}

struct listing { const char *parent;enum dt_state state;unsigned count; };

static void list_child(const char *path,const struct dt_node *node,void *context)
{
    struct listing *list=context;
    if (!dt_direct_child(path,list->parent)) { return; }
    enum dt_state state=dt_local_state(node);
    if (state==DT_ENABLED) { state=list->state; }
    console_write(node->name); console_write(" status="); show_status(state);
    show_compatible(node,true);
    console_write("\n");
    ++list->count;
}

static void show_cells(const unsigned char *p,uint32_t count)
{
    for (uint32_t i=0;i<count && i<PRINT_CELLS;++i) {
        console_write(" "); console_hex(dt_cell(p+4*i));
    }
    if (count>PRINT_CELLS) { console_write(" ..."); }
}

static void show_raw(const struct dt_node *node,const char *key)
{
    const unsigned char *p;
    uint32_t bytes;
    enum dt_lookup result=dt_node_property(node,key,&p,&bytes);
    if (result==DT_MISSING) { return; }
    console_write(key); console_write(":");
    if (result!=DT_FOUND || bytes%4) { console_write(" invalid"); }
    else if (!bytes) { console_write(" empty"); }
    else { show_cells(p,bytes/4); }
    console_write("\n");
}

static void show_interrupt_parent(const void *dtb,const char *path)
{
    char parent[DT_PATH_CAPACITY];
    char *out=parent;
    while ((*out++=*path++)) {}
    const unsigned char *p;
    uint32_t bytes;
    enum dt_lookup result;
    do {
        result=dt_optional_property(dtb,DT_READ_LIMIT,parent,"interrupt-parent",&p,&bytes);
    } while (result==DT_MISSING && dt_parent_path(parent));
    if (result==DT_MISSING) { return; }
    struct dt_node provider;
    console_write("interrupt-parent: ");
    if (result==DT_FOUND && bytes==4 &&
        dt_find_phandle(dtb,DT_READ_LIMIT,dt_cell(p),&provider,parent)==DT_FOUND) { console_write(parent); }
    else { console_write("unresolved"); }
    console_write("\n");
}

static void show_dependencies(const void *dtb,const struct dt_node *node)
{
    static const char *const props[][2]={
        {"clocks","#clock-cells"},{"resets","#reset-cells"},{"dmas","#dma-cells"},
        {"power-domains","#power-domain-cells"},{"phys","#phy-cells"},{"mboxes","#mbox-cells"},
        {"interrupts-extended","#interrupt-cells"},{"iommus","#iommu-cells"},
        {"pinctrl-0",NULL},{"vdd-supply",NULL},{"vmmc-supply",NULL},{"vqmmc-supply",NULL},
        {"phy-supply",NULL},{"firmware",NULL}
    };
    char path[DT_PATH_CAPACITY];
    for (unsigned i=0;i<sizeof(props)/sizeof(*props);++i) {
        const unsigned char *p;
        uint32_t bytes;
        enum dt_lookup result=dt_node_property(node,props[i][0],&p,&bytes);
        if (result==DT_MISSING) { continue; }
        if (result==DT_FOUND && !bytes) {
            console_write(props[i][0]); console_write(": empty\n");
            continue;
        }
        if (result!=DT_FOUND || bytes%4) {
            console_write(props[i][0]); console_write(": invalid\n");
            continue;
        }
        unsigned remaining=bytes/4,entries=0;
        while (remaining && entries<PRINT_RANGES) {
            struct dt_node provider;
            uint32_t id=dt_cell(p),cells=0,field_bytes;
            const unsigned char *field;
            console_write(props[i][0]); console_write(": ");
            if (dt_find_phandle(dtb,DT_READ_LIMIT,id,&provider,path)!=DT_FOUND ||
                (props[i][1] && (dt_node_property(&provider,props[i][1],&field,&field_bytes)!=DT_FOUND ||
                                field_bytes!=4 || (cells=dt_cell(field))>=remaining))) {
                console_write("unresolved phandle="); console_hex(id); console_write("\n");
                break;
            }
            console_write(path);
            show_cells(p+4,cells);
            console_write("\n");
            p+=4*(cells+1);
            remaining-=cells+1;
            ++entries;
        }
        if (remaining && entries==PRINT_RANGES) { console_write("Additional references omitted.\n"); }
    }
}

static void show_device(const void *dtb,const char *path,const struct dt_node *node,const struct board_info *board,
                        enum dt_state state)
{
    console_write(path); console_write(" status="); show_status(state);
    show_compatible(node,false);
    console_write("\n");
    bool console=false;
    for (unsigned i=0;i<=PRINT_RANGES;++i) {
        struct dt_reg_range range;
        enum dt_reg_result result=dt_reg_range(dtb,DT_READ_LIMIT,path,node,i,&range);
        if (result==DT_REG_END) { break; }
        if (result==DT_REG_RAW) { show_raw(node,"reg"); break; }
        if (i==PRINT_RANGES) { console_write("Additional reg entries omitted.\n"); break; }
        console_write("reg bus="); console_hex(range.address);
        console_write(" size="); console_hex(range.bytes);
        console_write(" dt_cpu=");
        if (result==DT_REG_CPU) {
            console_hex(range.cpu);
            console |= range.cpu==board->uart_base;
        } else { console_write("unresolved"); }
        console_write("\n");
    }
    if (same(board->name,"rpi5-header") && same(path,"/axi/pcie@1000120000/rp1/serial@30000")) {
        console=true;
    }
    if (console) {
        console_write("owner=serial-console live_mmio="); console_hex(board->uart_base); console_write("\n");
    }
    show_interrupt_parent(dtb,path);
    show_raw(node,"interrupts");
    show_raw(node,"ranges");
    show_raw(node,"dma-ranges");
    show_dependencies(dtb,node);
}

bool device_inventory_command(const char *line,const void *dtb,const struct board_info *board)
{
    const char *p=line;
    const char *name="dev";
    while (*name && *p==*name) { ++p; ++name; }
    if (*name) { return false; }
    bool list=p[0]=='l' && p[1]=='i' && p[2]=='s' && p[3]=='t';
    bool detail=p[0]=='i' && p[1]=='n' && p[2]=='f' && p[3]=='o';
    if ((!list && !detail) || (p[4] && p[4]!=' ')) { return false; }
    p+=4;
    while (*p==' ') { ++p; }
    if (!*p && list) { p="/"; }
    if (!*p) { console_write("Usage: devinfo <DT path|alias>\n"); return true; }
    const char *path=resolve(dtb,p);
    struct dt_node node;
    enum dt_lookup result=path ? dt_find_node(dtb,DT_READ_LIMIT,path,&node) : DT_MISSING;
    if (result!=DT_FOUND) {
        console_write(result==DT_INVALID ? "Device tree invalid or unavailable.\n" : "Device-tree node or alias not found.\n");
        return true;
    }
    enum dt_state state=dt_device_state(dtb,DT_READ_LIMIT,path);
    if (list) {
        console_write("DT children of "); console_write(path); console_write("\n");
        struct listing children={.parent=path,.state=state};
        (void)dt_walk(dtb,DT_READ_LIMIT,list_child,&children);
        console_write("Children: "); console_u64(children.count); console_write("\n");
    } else { show_device(dtb,path,&node,board,state); }
    return true;
}
