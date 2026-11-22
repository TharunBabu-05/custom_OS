#include "rp1_msix.h"
#include "rp1_probe.h"

enum {
    CONFIG_DATA=0x8000, CONFIG_INDEX=0x9000, LINK_STATUS=0x4068,
    OUTBOUND_COUNT=4, INBOUND_COUNT=10, RP1_BAR0_BYTES=0x4000
};

bool rp1_pci_description(const void *dtb,size_t bytes)
{
    const unsigned char *p;
    uint32_t n;
    static const char path[]="/axi/pcie@1000120000",type[]="brcm,bcm2712-pcie";
    if (!dt_property(dtb,bytes,path,"compatible",&p,&n) || n!=sizeof(type)) { return false; }
    for (unsigned i=0;i<n;++i) {
        if (p[i]!=(unsigned char)type[i]) { return false; }
    }
    return dt_property(dtb,bytes,path,"reg",&p,&n) && n==16 &&
           dt_cell(p)==0x10 && dt_cell(p+4)==0x00120000 &&
           !dt_cell(p+8) && dt_cell(p+12)==0x9310;
}

static bool read(const struct xhci_observer *io,uint32_t offset,uint32_t *value)
{
    return io && io->read32 && io->span>=4 && offset<=io->span-4 &&
           io->read32(io->context,offset,value) && *value!=UINT32_MAX;
}

static bool select_config(const struct xhci_observer *io,rp1_pci_write write,uint32_t index)
{
    uint32_t observed;
    return write(io->context,CONFIG_INDEX,index) && read(io,CONFIG_INDEX,&observed) && observed==index;
}

static bool capability(const struct xhci_observer *io,struct rp1_msix *out)
{
    uint32_t pointer;
    if (!read(io,CONFIG_DATA+0x34,&pointer)) { return false; }
    uint64_t visited=0;
    unsigned offset=pointer&255;
    while (offset) {
        if (offset<0x40 || offset>0xfc || (offset&3) || (visited>>(offset/4)&1)) { return false; }
        visited|=1ULL<<(offset/4);
        uint32_t header;
        if (!read(io,CONFIG_DATA+offset,&header)) { return false; }
        if ((header&255)==0x11) {
            uint32_t table,pba;
            if (offset>0xf4 || out->capability ||
                !read(io,CONFIG_DATA+offset+4,&table) || !read(io,CONFIG_DATA+offset+8,&pba)) { return false; }
            unsigned entries=((header>>16)&0x7ff)+1;
            /* The C0 BAR0 is 16 KiB and its PBA is outside the table's page. */
            unsigned table_offset=table&~7u,pba_offset=pba&~7u;
            unsigned pba_bytes=((entries+63)/64)*8;
            if ((table|pba)&7 || table_offset>=RP1_BAR0_BYTES || entries*16>RP1_BAR0_BYTES-table_offset ||
                pba_offset>=RP1_BAR0_BYTES || pba_bytes>RP1_BAR0_BYTES-pba_offset ||
                (table_offset<pba_offset+pba_bytes && pba_offset<table_offset+entries*16)) { return false; }
            out->capability=offset;
            out->control=header>>16;
            out->entries=entries;
            out->table_bus+=table_offset;
            out->pba_bus+=pba_offset;
        }
        offset=(header>>8)&255;
    }
    return out->capability!=0;
}

enum rp1_pci_result rp1_msix_read(const struct xhci_observer *io,rp1_pci_write write,
                                  struct rp1_pci_header *bridge,struct rp1_msix *out)
{
    struct rp1_pci_header root;
    if (!write || !out || !bridge || !read(io,LINK_STATUS,&root.link) ||
        !read(io,0x0c,&root.header) || !read(io,0x18,&root.buses)) { return RP1_PCI_BRIDGE; }
    *bridge=root;
    if ((root.link&0x70)!=0x30 || (!(root.link&0x80) && (root.header>>16&0x7f)!=1)) {
        return RP1_PCI_LINK;
    }
    unsigned primary=root.buses&255,secondary=root.buses>>8&255,subordinate=root.buses>>16&255;
    if (!secondary || primary==secondary || subordinate<secondary) { return RP1_PCI_BUS; }
    uint32_t selector;
    if (!read(io,CONFIG_INDEX,&selector)) { return RP1_PCI_SELECTOR; }

    struct rp1_msix result={.bus=secondary};
    enum rp1_pci_result status=RP1_PCI_SELECTOR;
    if (select_config(io,write,(uint32_t)result.bus<<20)) {
        status=RP1_PCI_ENDPOINT;
        uint32_t id,command,bar0,bar1;
        if (read(io,CONFIG_DATA,&id) && id==0x00011de4 &&
            read(io,CONFIG_DATA+4,&command) && (command&0x00100002)==0x00100002 &&
            read(io,CONFIG_DATA+0x10,&bar0) && !(bar0&7) &&
            read(io,CONFIG_DATA+0x14,&bar1) && !(bar1&7)) {
            result.table_bus=result.pba_bus=bar0&~15u;
            result.peripheral_bus=bar1&~15u;
            status=capability(io,&result) ? RP1_PCI_OK : RP1_PCI_CAPABILITY;
        }
    }
    if (!select_config(io,write,selector)) { return RP1_PCI_RESTORE; }
    if (status==RP1_PCI_OK) { *out=result; }
    return status;
}

bool rp1_pci_cpu_address(const struct xhci_observer *io,uint64_t bus,uint32_t bytes,uint64_t *cpu)
{
    if (!bytes || !cpu) { return false; }
    uint64_t result=0;
    bool found=false;
    for (unsigned i=0;i<OUTBOUND_COUNT;++i) {
        uint32_t low,high,base_limit,base_high,limit_high;
        if (!read(io,0x400c+8*i,&low) || !read(io,0x4010+8*i,&high) ||
            !read(io,0x4070+4*i,&base_limit) || !read(io,0x4080+8*i,&base_high) ||
            !read(io,0x4084+8*i,&limit_high)) { return false; }
        uint64_t start=(uint64_t)((base_high&255)<<12 | (base_limit>>4&0xfff))<<20;
        uint64_t limit=(uint64_t)((limit_high&255)<<12 | (base_limit>>20))<<20;
        uint64_t pci=(uint64_t)high<<32|low;
        if (!(start|limit|pci) || start>limit || bus<pci) { continue; }
        uint64_t span=limit-start+(1u<<20),delta=bus-pci;
        if (bytes>span || delta>span-bytes) { continue; }
        if (found) { return false; }
        found=true;
        result=start+delta;
    }
    if (found) { *cpu=result; }
    return found;
}

enum rp1_mip_aperture rp1_mip_aperture_read(const struct xhci_observer *io)
{
    const uint64_t doorbell=0xfffffff000ULL;
    bool covers=false,exact=false;
    for (unsigned i=0;i<INBOUND_COUNT;++i) {
        unsigned bar=i<3 ? 0x402c+8*i : 0x40d4+8*(i-3);
        unsigned remap=i<3 ? 0x40ac+8*i : 0x410c+8*(i-3);
        uint32_t low,high,cpu_low,cpu_high;
        if (!read(io,bar,&low) || !read(io,bar+4,&high) ||
            !read(io,remap,&cpu_low) || !read(io,remap+4,&cpu_high)) { return RP1_MIP_UNAVAILABLE; }
        unsigned size=low&31;
        if (!size || !(cpu_low&1)) { continue; }
        uint64_t bus=(uint64_t)high<<32|(low&~31u);
        unsigned shift=size>=28 ? size-16 : size+15;
        if (size>21 && size<28) { return RP1_MIP_UNAVAILABLE; }
        if (doorbell<bus || doorbell-bus>=(1ULL<<shift)) { continue; }
        if (covers) { return RP1_MIP_UNAVAILABLE; }
        covers=true;
        exact=size==28 && bus==doorbell && ((uint64_t)cpu_high<<32|cpu_low)==0x1000130001ULL;
    }
    if (exact) { return RP1_MIP_MAPPED; }
    return covers ? RP1_MIP_UNAVAILABLE : RP1_MIP_UNMAPPED;
}
