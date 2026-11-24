#include "dt_resources.h"
#include "rp1_probe.h"
#include "xhci_test.h"

#define BRIDGE "/axi/pcie@1000120000"
#define RP1 BRIDGE "/rp1"
#define DMA RP1 "/dma@188000"
#define GPIO RP1 "/gpio@d0000"
#define ETH RP1 "/ethernet@100000"
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

bool rp1_eth_description(const void *dtb,size_t bytes)
{
    static const char gem[]="raspberrypi,rp1-gem\0cdns,macb";
    struct dt_node eth,phy,clock;
    char phy_path[DT_PATH_CAPACITY];
    const unsigned char *p;
    uint32_t n,phy_id,phy_handle;
    if (dt_device_state(dtb,bytes,ETH)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,ETH,&eth)!=DT_FOUND || !compatible(&eth,gem,sizeof(gem)) ||
        dt_node_property(&eth,"reg",&p,&n)!=DT_FOUND || n!=16 ||
        dt_cell(p)!=0xc0 || dt_cell(p+4)!=0x40100000 || dt_cell(p+8) || dt_cell(p+12)!=RP1_ETH_SPAN ||
        dt_node_property(&eth,"phy-mode",&p,&n)!=DT_FOUND || n!=sizeof("rgmii-id")) { return false; }
    for (unsigned i=0;i<n;++i) { if (p[i]!=(unsigned char)"rgmii-id"[i]) { return false; } }
    return scalar(&eth,"phy-handle",&phy_handle) && dt_find_phandle(dtb,bytes,phy_handle,&phy,phy_path)==DT_FOUND &&
        dt_direct_child(phy_path,ETH) && dt_local_state(&phy)==DT_ENABLED && scalar(&phy,"reg",&phy_id) && phy_id==1 &&
        dt_node_property(&eth,"clocks",&p,&n)==DT_FOUND && n==32 &&
        dt_cell(p+4)==12 && dt_find_phandle(dtb,bytes,dt_cell(p),&clock,NULL)==DT_FOUND &&
        compatible(&clock,"raspberrypi,rp1-clocks",sizeof("raspberrypi,rp1-clocks"));
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
        RP1 "/serial@30000", RP1 "/usb@200000", RP1 "/usb@300000", ETH, DMA
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

bool rp1_eth_reset_description(const void *dtb,size_t bytes)
{
    struct dt_node gpio,eth;
    const unsigned char *p;
    uint32_t n,id,ms;
    return rp1_gpio_description(dtb,bytes) && dt_find_node(dtb,bytes,GPIO,&gpio)==DT_FOUND &&
        scalar(&gpio,"phandle",&id) && dt_find_phandle(dtb,bytes,id,&gpio,NULL)==DT_FOUND &&
        dt_find_node(dtb,bytes,ETH,&eth)==DT_FOUND && scalar(&eth,"phy-reset-duration",&ms) && ms==5 &&
        dt_node_property(&eth,"phy-reset-gpios",&p,&n)==DT_FOUND && n==12 &&
        dt_cell(p)==id && dt_cell(p+4)==RP1_ETH_RESET_GPIO && dt_cell(p+8)==1;
}

enum {
    RIO_OUT=0x10000, RIO_OE=0x10004, RIO_IN=0x10008,
    RIO_SET=0x2000, RIO_CLEAR=0x3000, PAD_VOLTAGE=0x20000,
    PAD_PULL_DOWN=1u<<2, PAD_PULL_UP=1u<<3, PAD_DRIVE=3u<<4,
    PAD_INPUT=1u<<6, PAD_OUTPUT_DISABLE=1u<<7,
    GPIO_FUNC=31, GPIO_RIO=5, GPIO_OVERRIDES=0x3f000, GPIO_IRQ_MASKS=0x0ff00000,
    STATUS_INFROMPAD=1u<<17, STATUS_OETOPAD=1u<<13, STATUS_OUTTOPAD=1u<<9
};

bool rp1_gpio_read(const struct xhci_observer *io,unsigned pin,struct rp1_gpio_snapshot *out)
{
    struct rp1_gpio_snapshot snapshot;
    uint32_t input,output,enable;
    if (!out || pin>=RP1_HEADER_GPIOS || !read(io,4+8*pin,&snapshot.control) ||
        !read(io,PAD_VOLTAGE+4+4*pin,&snapshot.pad) || !read(io,RIO_IN,&input) ||
        !read(io,RIO_OUT,&output) || !read(io,RIO_OE,&enable)) { return false; }
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

static bool gpio_isolated(const struct rp1_gpio_snapshot *pin)
{
    return (pin->control&(GPIO_IRQ_MASKS|0xc000|GPIO_FUNC))==GPIO_FUNC &&
           (pin->pad&PAD_OUTPUT_DISABLE) && !pin->output_enable;
}

enum rp1_gpio_result rp1_gpio_test(const struct xhci_observer *io,
                                   bool (*write)(void *,uint32_t,uint32_t),unsigned pin)
{
    struct rp1_gpio_snapshot original;
    if (!write || !rp1_gpio_read(io,pin,&original)) { return RP1_GPIO_IO; }
    if (!gpio_isolated(&original)) { return RP1_GPIO_BUSY; }
    uint32_t mask=1u<<pin;
    void *context=io->context;
    bool passed=write(context,RIO_OUT+RIO_CLEAR,mask) && gpio_state(io,pin,&original,false,false) &&
                write(context,RIO_OE+RIO_SET,mask) && gpio_state(io,pin,&original,false,true) &&
                write(context,RIO_OUT+RIO_SET,mask) && gpio_state(io,pin,&original,true,true) &&
                write(context,RIO_OUT+RIO_CLEAR,mask) && gpio_state(io,pin,&original,false,true);

    /* Clear the internal driver first. Attempt both restores even after a failed write. */
    bool disabled=write(context,RIO_OE+RIO_CLEAR,mask);
    bool restored=write(context,RIO_OUT+(original.output ? RIO_SET : RIO_CLEAR),mask);
    bool verified=gpio_state(io,pin,&original,original.output,false);
    if (!disabled || !restored || !verified) { return RP1_GPIO_RESTORE; }
    return passed ? RP1_GPIO_OK : RP1_GPIO_IO;
}

static enum rp1_gpio_result gpio_sample(const struct xhci_observer *io,unsigned pin,unsigned step,
                                       bool high,bool driving,struct rp1_gpio_sample *sample)
{
    unsigned stable=0;
    sample->step=step;
    sample->pin=pin;
    sample->expected=high;
    for (unsigned i=0;i<512 && stable<4;++i) {
        uint32_t input,status;
        if (!read(io,8*pin,&status) || !read(io,RIO_IN,&input)) { return RP1_GPIO_IO; }
        bool pad=(status&STATUS_INFROMPAD)!=0;
        bool rio=((input>>pin)&1)!=0;
        bool enabled=(status&STATUS_OETOPAD)!=0;
        bool output=(status&STATUS_OUTTOPAD)!=0;
        if (pad==high && rio==high && enabled==driving && (!driving || output==high)) { ++stable; }
        else {
            stable=0;
            sample->pad=pad;
            sample->rio=rio;
            sample->status=status;
        }
    }
    return stable==4 ? RP1_GPIO_OK : RP1_GPIO_SIGNAL;
}

/* The observer starts at GPIO bank 1. GPIO32 is bit 4 in that bank. */
enum { ETH_RESET_BIT=4, ETH_RESET_CTRL=4+8*ETH_RESET_BIT, ETH_RESET_PAD=PAD_VOLTAGE+4+4*ETH_RESET_BIT };

bool rp1_eth_reset_restore(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                           const struct rp1_gpio_snapshot *saved)
{
    const uint32_t mask=1u<<ETH_RESET_BIT;
    void *context=io->context;
    bool restored=write(context,ETH_RESET_PAD,saved->pad|PAD_OUTPUT_DISABLE);
    restored&=write(context,RIO_OE+RIO_CLEAR,mask);
    restored&=write(context,RIO_OUT+(saved->output ? RIO_SET : RIO_CLEAR),mask);
    restored&=write(context,ETH_RESET_CTRL,saved->control);
    if (saved->output_enable) { restored&=write(context,RIO_OE+RIO_SET,mask); }
    restored&=write(context,ETH_RESET_PAD,saved->pad);
    return restored && gpio_state(io,ETH_RESET_BIT,saved,saved->output,saved->output_enable);
}

static bool reset_delay(const struct xhci_observer *io,uint64_t (*ticks)(void *),uint64_t duration)
{
    uint64_t start=ticks(io->context);
    for (unsigned spin=0;spin<100000000;++spin) {
        if (ticks(io->context)-start>=duration) { return true; }
    }
    return false;
}

enum rp1_gpio_result rp1_eth_reset(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                   uint64_t (*ticks)(void *),uint64_t hz,struct rp1_gpio_snapshot *saved)
{
    if (!write || !ticks || hz<1000 || !rp1_gpio_read(io,ETH_RESET_BIT,saved)) { return RP1_GPIO_IO; }
    unsigned func=saved->control&GPIO_FUNC;
    if ((func!=GPIO_FUNC && func!=GPIO_RIO) || (saved->control&(GPIO_OVERRIDES|GPIO_IRQ_MASKS))) { return RP1_GPIO_BUSY; }
    const uint32_t mask=1u<<ETH_RESET_BIT;
    void *context=io->context;
    struct rp1_gpio_snapshot state=*saved;
    state.control=(saved->control&~GPIO_FUNC)|GPIO_RIO;
    state.pad=(saved->pad|PAD_INPUT)&~PAD_OUTPUT_DISABLE;
    struct rp1_gpio_sample sample;
    if (write(context,ETH_RESET_PAD,state.pad|PAD_OUTPUT_DISABLE) && write(context,RIO_OE+RIO_CLEAR,mask) &&
        write(context,RIO_OUT+RIO_CLEAR,mask) && write(context,ETH_RESET_CTRL,state.control) &&
        write(context,RIO_OE+RIO_SET,mask) && write(context,ETH_RESET_PAD,state.pad) &&
        gpio_state(io,ETH_RESET_BIT,&state,false,true) && gpio_sample(io,ETH_RESET_BIT,0,false,true,&sample)==RP1_GPIO_OK &&
        reset_delay(io,ticks,hz/200+(hz%200!=0)) && write(context,RIO_OUT+RIO_SET,mask) &&
        gpio_state(io,ETH_RESET_BIT,&state,true,true) && gpio_sample(io,ETH_RESET_BIT,1,true,true,&sample)==RP1_GPIO_OK &&
        reset_delay(io,ticks,hz/10+(hz%10!=0))) { return RP1_GPIO_OK; }
    return rp1_eth_reset_restore(io,write,saved) ? RP1_GPIO_IO : RP1_GPIO_RESTORE;
}

enum rp1_gpio_result rp1_gpio_pull(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),unsigned pin,
                                   struct rp1_gpio_sample *sample)
{
    struct rp1_gpio_snapshot original;
    if (!write || !sample || !rp1_gpio_read(io,pin,&original)) { return RP1_GPIO_IO; }
    if (!gpio_isolated(&original)) { return RP1_GPIO_BUSY; }

    const unsigned ctrl=4+8*pin,pad=PAD_VOLTAGE+4+4*pin;
    struct rp1_gpio_snapshot config=original;
    config.control=(config.control&~(GPIO_FUNC|GPIO_OVERRIDES))|GPIO_RIO;
    void *context=io->context;
    enum rp1_gpio_result result=RP1_GPIO_IO;
    if (write(context,ctrl,config.control)) {
        for (unsigned step=0;step<3;++step) {
            bool high=step==1;
            config.pad=(original.pad&~(PAD_PULL_DOWN|PAD_PULL_UP))|PAD_INPUT|(high ? PAD_PULL_UP : PAD_PULL_DOWN);
            if (!write(context,pad,config.pad) || !gpio_state(io,pin,&config,original.output,false)) {
                result=RP1_GPIO_IO;
                break;
            }
            result=gpio_sample(io,pin,step,high,false,sample);
            if (result!=RP1_GPIO_OK) { break; }
        }
    }
    bool restored=write(context,ctrl,original.control);
    restored&=write(context,pad,original.pad);
    restored&=gpio_state(io,pin,&original,original.output,false);
    return restored ? result : RP1_GPIO_RESTORE;
}

static enum rp1_gpio_result gpio_levels(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                       const struct rp1_gpio_snapshot *tx,const struct rp1_gpio_snapshot *rx,
                                       struct rp1_gpio_sample *sample)
{
    const uint32_t tx_mask=1u<<RP1_LOOP_TX;
    for (unsigned step=0;step<5;++step) {
        bool high=(step&1)!=0;
        if (!write(io->context,RIO_OUT+(high ? RIO_SET : RIO_CLEAR),tx_mask) ||
            !gpio_state(io,RP1_LOOP_TX,tx,high,true)) { return RP1_GPIO_IO; }
        enum rp1_gpio_result result=gpio_sample(io,RP1_LOOP_TX,step,high,true,sample);
        if (result!=RP1_GPIO_OK) { return result; }
        result=gpio_sample(io,RP1_LOOP_RX,step,high,false,sample);
        if (result!=RP1_GPIO_OK) { return result; }
        if (!gpio_state(io,RP1_LOOP_RX,rx,rx->output,false)) { return RP1_GPIO_IO; }
    }
    return RP1_GPIO_OK;
}

enum rp1_gpio_result rp1_gpio_loop(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                   struct rp1_gpio_sample *sample)
{
    struct rp1_gpio_snapshot tx,rx;
    uint32_t voltage;
    if (!write || !sample || !rp1_gpio_read(io,RP1_LOOP_TX,&tx) || !rp1_gpio_read(io,RP1_LOOP_RX,&rx) ||
        !read(io,PAD_VOLTAGE,&voltage)) { return RP1_GPIO_IO; }
    if (!gpio_isolated(&tx) || !gpio_isolated(&rx) || (voltage&1)) { return RP1_GPIO_BUSY; }

    const unsigned tx_ctrl=4+8*RP1_LOOP_TX, rx_ctrl=4+8*RP1_LOOP_RX;
    const unsigned tx_pad=PAD_VOLTAGE+4+4*RP1_LOOP_TX, rx_pad=PAD_VOLTAGE+4+4*RP1_LOOP_RX;
    const uint32_t tx_mask=1u<<RP1_LOOP_TX;
    struct rp1_gpio_snapshot tx_config=tx,rx_config=rx;
    tx_config.control=(tx.control&~(GPIO_FUNC|GPIO_OVERRIDES))|GPIO_RIO;
    tx_config.pad=(tx.pad&~PAD_DRIVE)|PAD_INPUT;
    rx_config.control=(rx.control&~(GPIO_FUNC|GPIO_OVERRIDES))|GPIO_RIO;
    rx_config.pad=(rx.pad&~PAD_PULL_UP)|PAD_INPUT|PAD_PULL_DOWN;
    void *context=io->context;

    enum rp1_gpio_result result=RP1_GPIO_IO;
    /* Verify an input-only receiver and a low transmitter before enabling the pad. */
    if (write(context,rx_pad,rx_config.pad) && write(context,rx_ctrl,rx_config.control) &&
        gpio_state(io,RP1_LOOP_RX,&rx_config,rx.output,false) &&
        write(context,tx_pad,tx_config.pad) && write(context,tx_ctrl,tx_config.control) &&
        write(context,RIO_OUT+RIO_CLEAR,tx_mask) && gpio_state(io,RP1_LOOP_TX,&tx_config,false,false) &&
        write(context,RIO_OE+RIO_SET,tx_mask) && gpio_state(io,RP1_LOOP_TX,&tx_config,false,true)) {
        tx_config.pad&=~PAD_OUTPUT_DISABLE;
        if (write(context,tx_pad,tx_config.pad)) { result=gpio_levels(io,write,&tx_config,&rx_config,sample); }
    }

    /* Try every restore, starting by disconnecting the output. */
    bool restored=write(context,tx_pad,tx.pad);
    restored&=write(context,RIO_OE+RIO_CLEAR,tx_mask);
    restored&=write(context,tx_ctrl,tx.control);
    restored&=write(context,RIO_OUT+(tx.output ? RIO_SET : RIO_CLEAR),tx_mask);
    restored&=write(context,rx_ctrl,rx.control);
    restored&=write(context,rx_pad,rx.pad);
    restored&=gpio_state(io,RP1_LOOP_TX,&tx,tx.output,false);
    restored&=gpio_state(io,RP1_LOOP_RX,&rx,rx.output,false);
    return restored ? result : RP1_GPIO_RESTORE;
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
