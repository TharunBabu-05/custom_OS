#include "pi5_pcie.h"

static bool cell(const struct dt_node *node,const char *key,uint32_t *value)
{
    const unsigned char *p;
    uint32_t n;
    if (dt_node_property(node,key,&p,&n)!=DT_FOUND || n!=4) { return false; }
    *value=dt_cell(p);
    return true;
}

static bool string(const struct dt_node *node,const char *key,const char *value,size_t length)
{
    const unsigned char *p;
    uint32_t n;
    if (dt_node_property(node,key,&p,&n)!=DT_FOUND || n!=length) { return false; }
    for (uint32_t i=0;i<n;++i) { if (p[i]!=(unsigned char)value[i]) { return false; } }
    return true;
}

bool pi5_pcie_describe(const void *dtb,size_t bytes,enum pi5_pcie_port port,struct pi5_pcie_bridge *out)
{
    if (port!=PI5_PCIE_EXTERNAL && port!=PI5_PCIE_RP1) { return false; }
    bool external=port==PI5_PCIE_EXTERNAL;
    const char *path=external ? "/axi/pcie@1000110000" : "/axi/pcie@1000120000";
    struct pi5_pcie_bridge host;
    struct dt_reg_range reg;
    uint32_t addresses,sizes;
    if (dt_find_node(dtb,bytes,path,&host.node)!=DT_FOUND ||
        !string(&host.node,"compatible","brcm,bcm2712-pcie",sizeof("brcm,bcm2712-pcie")) ||
        !string(&host.node,"device_type","pci",sizeof("pci")) ||
        !cell(&host.node,"#address-cells",&addresses) || addresses!=3 ||
        !cell(&host.node,"#size-cells",&sizes) || sizes!=2 ||
        !cell(&host.node,"linux,pci-domain",&host.domain) || host.domain!=(external ? 1u : 2u) ||
        !cell(&host.node,"num-lanes",&host.lanes) || host.lanes!=(external ? 1u : 4u) ||
        !cell(&host.node,"max-link-speed",&host.generation) || !host.generation || host.generation>3 ||
        dt_reg_range(dtb,bytes,path,&host.node,0,&reg)!=DT_REG_CPU ||
        reg.cpu!=(external ? 0x1000110000ULL : 0x1000120000ULL) || reg.bytes!=0x9310 ||
        dt_reg_range(dtb,bytes,path,&host.node,1,&reg)!=DT_REG_END) { return false; }
    host.state=dt_device_state(dtb,bytes,path);
    if (host.state==DT_BAD_STATUS) { return false; }
    host.registers=reg.cpu;
    *out=host;
    return true;
}

static uint64_t pair(const unsigned char *p)
{
    return (uint64_t)dt_cell(p)<<32|dt_cell(p+4);
}

static bool overlaps(uint64_t first,uint64_t first_size,uint64_t second,uint64_t second_size)
{
    return first<=second ? second-first<first_size : first-second<second_size;
}

unsigned pi5_pcie_windows(const void *dtb,size_t bytes,const struct pi5_pcie_bridge *host,bool inbound,
                          struct pi5_pcie_window out[PI5_PCIE_MAX_WINDOWS])
{
    const char *upstream=inbound ? "dma-ranges" : "ranges";
    const unsigned char *p;
    uint32_t n;
    struct dt_node axi;
    uint32_t addresses,sizes;
    enum { STRIDE=28, SPACE=0x03000000, MEM32=0x02000000, PREFETCH=0x40000000 };
    if (dt_find_node(dtb,bytes,"/axi",&axi)!=DT_FOUND ||
        !cell(&axi,"#address-cells",&addresses) || addresses!=2 ||
        !cell(&axi,"#size-cells",&sizes) || sizes!=2 ||
        dt_node_property(&host->node,upstream,&p,&n)!=DT_FOUND ||
        !n || n%STRIDE || n/STRIDE>(inbound ? PI5_PCIE_MAX_WINDOWS : 4u)) { return 0; }
    unsigned count=n/STRIDE;
    for (unsigned i=0;i<count;++i,p+=STRIDE) {
        uint32_t flags=dt_cell(p);
        struct pi5_pcie_window w={.pci=pair(p+4),.bytes=pair(p+20),
                                 .wide=(flags&SPACE)==SPACE,.prefetch=(flags&PREFETCH)!=0};
        uint64_t max=w.wide ? UINT64_MAX : UINT32_MAX;
        if ((flags&~(SPACE|PREFETCH)) || (flags&SPACE)<MEM32 ||
            !w.bytes || w.pci>max || w.bytes-1>max-w.pci ||
            !dt_bus_translate(dtb,bytes,"/axi",upstream,pair(p+12),w.bytes,&w.cpu)) { return 0; }
        for (unsigned j=0;j<i;++j) {
            if (overlaps(w.pci,w.bytes,out[j].pci,out[j].bytes) ||
                overlaps(w.cpu,w.bytes,out[j].cpu,out[j].bytes)) { return 0; }
        }
        out[i]=w;
    }
    return count;
}
