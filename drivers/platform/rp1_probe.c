#include "dt_resources.h"
#include "rp1_probe.h"
#include "xhci_test.h"

#define BRIDGE "/axi/pcie@1000120000"
#define RP1 BRIDGE "/rp1"
#define DMA RP1 "/dma@188000"
#define GPIO RP1 "/gpio@d0000"
#define GIC "/soc@107c000000/interrupt-controller@7fff9000"

static bool scalar(const struct dt_node *node,const char *key,uint32_t *out)
{
    const unsigned char *p;
    uint32_t bytes;
    if (dt_node_property(node,key,&p,&bytes)!=DT_FOUND || bytes!=4) { return false; }
    *out=dt_cell(p);
    return true;
}

static bool compatible(const struct dt_node *node,const char *name,size_t length)
{
    const unsigned char *p;
    uint32_t n;
    if (dt_node_property(node,"compatible",&p,&n)!=DT_FOUND || n!=length) { return false; }
    for (unsigned i=0;i<n;++i) {
        if (p[i]!=(unsigned char)name[i]) { return false; }
    }
    return true;
}

bool rp1_dma_description(const void *dtb,size_t bytes)
{
    struct dt_node node;
    const unsigned char *p;
    uint32_t n,value;
    if (dt_device_state(dtb,bytes,DMA)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,DMA,&node)!=DT_FOUND ||
        !compatible(&node,"snps,axi-dma-1.01a",sizeof("snps,axi-dma-1.01a"))) { return false; }
    return dt_node_property(&node,"reg",&p,&n)==DT_FOUND && n==16 &&
           dt_cell(p)==0xc0 && dt_cell(p+4)==0x40188000 && !dt_cell(p+8) && dt_cell(p+12)==0x1000 &&
           scalar(&node,"dma-channels",&value) && value==8 &&
           scalar(&node,"snps,dma-masters",&value) && value==1;
}

bool rp1_mip_describe(const void *dtb,size_t bytes,const char *path,struct rp1_mip *out)
{
    struct dt_node mip,gic;
    struct dt_reg_range control;
    const unsigned char *p;
    uint32_t n,id,offset=0;
    if (!out || dt_device_state(dtb,bytes,path)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,path,&mip)!=DT_FOUND ||
        !compatible(&mip,"brcm,bcm2712-mip",sizeof("brcm,bcm2712-mip")) ||
        dt_node_property(&mip,"msi-controller",&p,&n)!=DT_FOUND || n ||
        dt_reg_range(dtb,bytes,path,&mip,0,&control)!=DT_REG_CPU ||
        control.cpu!=0x1000130000ULL || control.bytes!=0xc0 ||
        dt_node_property(&mip,"reg",&p,&n)!=DT_FOUND || n!=32 ||
        dt_cell(p+16)!=0xff || dt_cell(p+20)!=0xfffff000 || dt_cell(p+24) || dt_cell(p+28)!=0x1000 ||
        dt_find_node(dtb,bytes,GIC,&gic)!=DT_FOUND ||
        !scalar(&gic,"phandle",&id) || !id || id==UINT32_MAX ||
        dt_find_phandle(dtb,bytes,id,&gic,NULL)!=DT_FOUND ||
        dt_node_property(&mip,"msi-ranges",&p,&n)!=DT_FOUND || n!=20 ||
        dt_cell(p)!=id || dt_cell(p+4)!=0 || dt_cell(p+12)!=1) { return false; }

    unsigned spi=dt_cell(p+8),count=dt_cell(p+16);
    enum dt_lookup offset_property=dt_node_property(&mip,"brcm,msi-offset",&p,&n);
    if (offset_property!=DT_MISSING) {
        if (offset_property!=DT_FOUND || n!=4) { return false; }
        offset=dt_cell(p);
    }
    if (spi!=128 || !count || count>64 || offset>64-count) { return false; }
    *out=(struct rp1_mip){.doorbell=0xfffffff000ULL,.first_intid=32+spi+offset,
                          .vectors=count,.data_offset=offset};
    return true;
}

bool rp1_mip_read(const struct xhci_observer *io,struct rp1_mip_snapshot *out)
{
    if (!io || !out || !io->read32 || io->span<0x94) { return false; }
    struct rp1_mip_snapshot snapshot;
    uint64_t *fields[]={&snapshot.edge,&snapshot.host_mask,&snapshot.vpu_mask,&snapshot.host_pending};
    for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        unsigned offset=0x20+i*0x20;
        uint32_t low,high;
        if (!io->read32(io->context,offset,&low) || !io->read32(io->context,offset+0x10,&high)) { return false; }
        *fields[i]=(uint64_t)high<<32|low;
    }
    *out=snapshot;
    return true;
}

bool rp1_topology_read(const void *dtb,size_t bytes,struct rp1_topology *out)
{
    struct dt_node bus,bridge,mip;
    const unsigned char *p;
    uint32_t n,provider,id,parent,count;
    if (!out || !pi5_dma_description(dtb,bytes) || dt_device_state(dtb,bytes,RP1)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,BRIDGE,&bridge)!=DT_FOUND || dt_find_node(dtb,bytes,RP1,&bus)!=DT_FOUND ||
        dt_node_property(&bus,"interrupt-controller",&p,&n)!=DT_FOUND || n ||
        !scalar(&bus,"#interrupt-cells",&count) || count!=2 ||
        !scalar(&bus,"phandle",&id) || !id || id==UINT32_MAX ||
        !scalar(&bus,"interrupt-parent",&parent) || parent!=id ||
        !scalar(&bridge,"msi-parent",&provider)) { return false; }

    struct rp1_topology result={0};
    if (dt_find_phandle(dtb,bytes,id,&mip,NULL)!=DT_FOUND ||
        dt_find_phandle(dtb,bytes,provider,&mip,result.msi_parent)!=DT_FOUND ||
        dt_node_property(&mip,"msi-controller",&p,&n)!=DT_FOUND || n) { return false; }
    static const char *const paths[RP1_ROUTE_COUNT]={
        RP1 "/serial@30000", RP1 "/usb@200000", RP1 "/usb@300000", RP1 "/ethernet@100000", DMA
    };
    for (unsigned i=0;i<RP1_ROUTE_COUNT;++i) {
        struct dt_node device;
        if (dt_find_node(dtb,bytes,paths[i],&device)!=DT_FOUND ||
            dt_node_property(&device,"interrupts-extended",&p,&n)!=DT_MISSING ||
            dt_node_property(&device,"interrupts",&p,&n)!=DT_FOUND || n!=8) { continue; }
        uint32_t line=dt_cell(p),trigger=dt_cell(p+4);
        enum dt_lookup explicit_parent=dt_node_property(&device,"interrupt-parent",&p,&n);
        if (explicit_parent!=DT_MISSING &&
            (explicit_parent!=DT_FOUND || n!=4 || dt_cell(p)!=id)) { continue; }
        if (line>=61 || (trigger!=1 && trigger!=4)) { continue; }
        result.routes[i]=(struct rp1_route){line,trigger};
        if (i==RP1_DMA && scalar(&device,"dma-channels",&count) && count==8) { result.dma_channels=count; }
    }
    *out=result;
    return true;
}

static bool read(const struct xhci_observer *port,uint32_t offset,uint32_t *value)
{
    return port && port->read32 && offset<=port->span && port->span-offset>=4 &&
           port->read32(port->context,offset,value) && *value!=UINT32_MAX;
}

bool rp1_gpio_description(const void *dtb,size_t bytes)
{
    struct dt_node node;
    const unsigned char *p;
    uint32_t n;
    if (dt_device_state(dtb,bytes,GPIO)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,GPIO,&node)!=DT_FOUND ||
        !compatible(&node,"raspberrypi,rp1-gpio",sizeof("raspberrypi,rp1-gpio")) ||
        dt_node_property(&node,"gpio-controller",&p,&n)!=DT_FOUND || n ||
        dt_node_property(&node,"reg",&p,&n)!=DT_FOUND || n<48 || n%16) { return false; }
    for (unsigned i=0;i<3;++i,p+=16) {
        if (dt_cell(p)!=0xc0 || dt_cell(p+4)!=0x40000000+RP1_GPIO_OFFSET+i*0x10000 ||
            dt_cell(p+8) || dt_cell(p+12)!=0xc000) { return false; }
    }
    return true;
}

bool rp1_gpio_read(const struct xhci_observer *io,unsigned pin,struct rp1_gpio_snapshot *out)
{
    struct rp1_gpio_snapshot snapshot;
    uint32_t input,output,enable;
    if (!out || pin>=RP1_HEADER_GPIOS || !read(io,4+8*pin,&snapshot.control) ||
        !read(io,0x20004+4*pin,&snapshot.pad) || !read(io,0x10008,&input) ||
        !read(io,0x10000,&output) || !read(io,0x10004,&enable)) { return false; }
    snapshot.input=(input>>pin)&1;
    snapshot.output=(output>>pin)&1;
    snapshot.output_enable=(enable>>pin)&1;
    *out=snapshot;
    return true;
}

static bool gpio_state(const struct xhci_observer *io,unsigned pin,const struct rp1_gpio_snapshot *original,
                       bool output,bool enable)
{
    struct rp1_gpio_snapshot actual;
    return rp1_gpio_read(io,pin,&actual) && actual.control==original->control && actual.pad==original->pad &&
           actual.output==output && actual.output_enable==enable;
}

enum rp1_gpio_result rp1_gpio_test(const struct xhci_observer *io,
                                   bool (*write)(void *,uint32_t,uint32_t),unsigned pin)
{
    enum { RIO_OUT_SET=0x12000, RIO_OE_SET=0x12004, RIO_OUT_CLEAR=0x13000, RIO_OE_CLEAR=0x13004 };
    struct rp1_gpio_snapshot original;
    if (!write || !rp1_gpio_read(io,pin,&original)) { return RP1_GPIO_IO; }
    /* The function must be disconnected, the pad output disabled, and RIO OE clear. */
    if ((original.control&0xc01f)!=31 || !(original.pad&0x80) || original.output_enable) { return RP1_GPIO_BUSY; }
    uint32_t mask=1u<<pin;
    void *context=io->context;
    bool passed=write(context,RIO_OUT_CLEAR,mask) && gpio_state(io,pin,&original,false,false) &&
                write(context,RIO_OE_SET,mask) && gpio_state(io,pin,&original,false,true) &&
                write(context,RIO_OUT_SET,mask) && gpio_state(io,pin,&original,true,true) &&
                write(context,RIO_OUT_CLEAR,mask) && gpio_state(io,pin,&original,false,true);

    /* Clear the internal driver first. Attempt both restores even after a failed write. */
    bool disabled=write(context,RIO_OE_CLEAR,mask);
    bool restored=write(context,original.output ? RIO_OUT_SET : RIO_OUT_CLEAR,mask);
    bool verified=gpio_state(io,pin,&original,original.output,false);
    if (!disabled || !restored || !verified) { return RP1_GPIO_RESTORE; }
    return passed ? RP1_GPIO_OK : RP1_GPIO_IO;
}

enum rp1_snapshot_result rp1_reachable(const struct xhci_observer *bridge,uint32_t offset)
{
    uint32_t status;
    if (!read(bridge,0x4068,&status)) { return RP1_SNAPSHOT_IO; }
    if ((status&0x30)!=0x30 || (status&0x40)) { return RP1_SNAPSHOT_LINK; }
    for (unsigned i=0;i<4;++i) {
        uint32_t low,base,limit;
        if (!read(bridge,0x4070+4*i,&low) || !read(bridge,0x4080+8*i,&base) ||
            !read(bridge,0x4084+8*i,&limit)) { return RP1_SNAPSHOT_IO; }
        /* The target and live UART must share the firmware outbound window.
         * Bridge base and inclusive limit are specified in 1 MiB units. */
        uint32_t start=((base&255)<<12)|((low>>4)&0xfff);
        uint32_t end=((limit&255)<<12)|(low>>20);
        if (start<=RP1_LIVE_BASE>>20 && end>=(RP1_LIVE_BASE+offset)>>20) {
            return RP1_SNAPSHOT_OK;
        }
    }
    return RP1_SNAPSHOT_WINDOW;
}

enum rp1_snapshot_result rp1_irq_read(const struct xhci_observer *bridge,const struct xhci_observer *apbs,
                                      const struct rp1_topology *topology,struct rp1_irq_snapshot *out)
{
    uint32_t low,high;
    if (!topology || !out) { return RP1_SNAPSHOT_IO; }
    enum rp1_snapshot_result window=rp1_reachable(bridge,RP1_PCIE_APBS);
    if (window!=RP1_SNAPSHOT_OK) { return window; }
    if (!read(apbs,0x108,&low) || !read(apbs,0x10c,&high)) { return RP1_SNAPSHOT_IO; }
    struct rp1_irq_snapshot snapshot={.raw=(uint64_t)high<<32 | low};
    for (unsigned i=0;i<RP1_ROUTE_COUNT;++i) {
        struct rp1_route route=topology->routes[i];
        if (!route.trigger) { continue; }
        if (route.line>=61 || !read(apbs,8+4u*route.line,&snapshot.config[i])) { return RP1_SNAPSHOT_IO; }
    }
    *out=snapshot;
    return RP1_SNAPSHOT_OK;
}
