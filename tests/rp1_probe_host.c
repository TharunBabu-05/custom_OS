#include "rp1_probe.h"
#include "rp1_msix.h"
#include "xhci_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct bridge {
    uint32_t status;
    bool covered;
};

static bool read_bridge(void *context,uint32_t offset,uint32_t *value)
{
    const struct bridge *bridge=context;
    if (offset==0x4068) { *value=bridge->status; return true; }
    if (offset==0x4070) { *value=bridge->covered ? 0x00300000u : 0; return true; }
    if (offset==0x4080 || offset==0x4084) { *value=0x1c; return true; }
    assert((offset>=0x4074 && offset<=0x407c) || (offset>=0x4088 && offset<=0x409c));
    *value=0;
    return true;
}

struct endpoint { unsigned reads; bool broken; };

static bool read_apbs(void *context,uint32_t offset,uint32_t *value)
{
    struct endpoint *endpoint=context;
    ++endpoint->reads;
    if (endpoint->broken) { *value=UINT32_MAX; return true; }
    switch (offset) {
    case 0x108: *value=1u<<25; break;
    case 0x10c: *value=(1u<<(36-32)) | (1u<<(40-32)); break;
    case 8+4*25: *value=9; break;
    case 8+4*31: *value=1; break;
    case 8+4*36: case 8+4*6: case 8+4*40: *value=0; break;
    default: assert(false);
    }
    return true;
}

static void test_snapshot(const struct rp1_topology *topology)
{
    struct bridge bridge={.status=0x30,.covered=true};
    struct endpoint endpoint={0};
    const struct xhci_observer rc={read_bridge,&bridge,0x9310},apbs={read_apbs,&endpoint,0x110};
    struct rp1_irq_snapshot snapshot;

    assert(rp1_irq_read(&rc,&apbs,topology,&snapshot)==RP1_SNAPSHOT_OK);
    assert(endpoint.reads==7);
    assert(snapshot.raw==((1ULL<<25)|(1ULL<<36)|(1ULL<<40)));
    assert(snapshot.config[RP1_UART]==9 && snapshot.config[RP1_USB0]==1 && !snapshot.config[RP1_USB1]);
    bridge.status=0x20;
    assert(rp1_irq_read(&rc,&apbs,topology,&snapshot)==RP1_SNAPSHOT_LINK && endpoint.reads==7);
    bridge.status=0x70;
    assert(rp1_irq_read(&rc,&apbs,topology,&snapshot)==RP1_SNAPSHOT_LINK && endpoint.reads==7);
    bridge.status=0x30;
    bridge.covered=false;
    assert(rp1_irq_read(&rc,&apbs,topology,&snapshot)==RP1_SNAPSHOT_WINDOW && endpoint.reads==7);
    bridge.covered=true;
    endpoint.broken=true;
    assert(rp1_irq_read(&rc,&apbs,topology,&snapshot)==RP1_SNAPSHOT_IO && endpoint.reads==8);
    assert(snapshot.raw==((1ULL<<25)|(1ULL<<36)|(1ULL<<40)));
    struct xhci_observer truncated=apbs;
    truncated.span=0x108;
    assert(rp1_irq_read(&rc,&truncated,topology,&snapshot)==RP1_SNAPSHOT_IO && endpoint.reads==8);
}

static bool read_mip(void *context,uint32_t offset,uint32_t *value)
{
    unsigned *count=context;
    ++*count;
    assert(offset>=0x20 && offset<=0x90 && !(offset&15));
    *value=offset==0x40 || offset==0x50 ? UINT32_MAX : offset;
    return true;
}

static void test_mip(unsigned char *dtb,size_t bytes,const char *path)
{
    struct rp1_mip mip;
    assert(rp1_mip_describe(dtb,bytes,path,&mip));
    assert(mip.doorbell==0xfffffff000ULL && mip.first_intid==160 && mip.vectors==64 && !mip.data_offset);
    assert(!rp1_mip_describe(dtb,bytes-1,path,&mip));
    const unsigned char *p;
    uint32_t n;
    assert(dt_property(dtb,bytes,path,"msi-ranges",&p,&n) && n==20);
    dtb[p-dtb+15]=4;
    assert(!rp1_mip_describe(dtb,bytes,path,&mip));
    dtb[p-dtb+15]=1;
    assert(dt_property(dtb,bytes,path,"brcm,msi-offset",&p,&n) && n==4);
    dtb[p-dtb+3]=1;
    assert(!rp1_mip_describe(dtb,bytes,path,&mip)); /* 64 vectors cannot start at 1. */
    dtb[p-dtb+3]=0;

    unsigned reads=0;
    struct xhci_observer io={read_mip,&reads,0x90};
    struct rp1_mip_snapshot snapshot;
    assert(!rp1_mip_read(&io,&snapshot) && !reads);
    io.span=0xc0;
    assert(rp1_mip_read(&io,&snapshot) && reads==8);
    assert(snapshot.edge==0x0000003000000020ULL && snapshot.host_mask==UINT64_MAX);
    assert(snapshot.vpu_mask==0x0000007000000060ULL && snapshot.host_pending==0x0000009000000080ULL);
}

static bool read_gpio(void *context,uint32_t offset,uint32_t *value)
{
    ++*(unsigned *)context;
    switch (offset) {
    case 4+8*27: *value=0xc005; break;
    case 0x20004+4*27: *value=0x48; break;
    case 0x10008: case 0x10004: *value=1u<<27; break;
    case 0x10000: *value=1u<<26; break;
    default: assert(false);
    }
    return true;
}

static void test_gpio(unsigned char *dtb,size_t bytes)
{
    const char *path="/axi/pcie@1000120000/rp1/gpio@d0000";
    assert(rp1_gpio_description(dtb,bytes) && !rp1_gpio_description(dtb,bytes-1));
    const unsigned char *property;
    uint32_t count;
    assert(dt_property(dtb,bytes,path,"reg",&property,&count) && count>=48);
    size_t pad_address=property-dtb+32+5;
    unsigned char original=dtb[pad_address];
    dtb[pad_address]^=1;
    assert(!rp1_gpio_description(dtb,bytes));
    dtb[pad_address]=original;

    unsigned reads=0;
    struct xhci_observer io={read_gpio,&reads,RP1_GPIO_SPAN};
    struct rp1_gpio_snapshot snapshot={0};
    assert(!rp1_gpio_read(&io,28,&snapshot) && !reads);
    assert(rp1_gpio_read(&io,27,&snapshot) && reads==5);
    assert(snapshot.control==0xc005 && snapshot.pad==0x48);
    assert(snapshot.input && snapshot.output_enable && !snapshot.output);
    --io.span;
    assert(!rp1_gpio_read(&io,27,&snapshot) && reads==6 && snapshot.pad==0x48);
}

enum { GPIO_PIN=17, GPIO_MASK=1u<<GPIO_PIN, OTHER_PIN=1u<<14 };
struct gpio_rio { uint32_t control,pad,out,oe; unsigned writes,drop_write; };

static bool read_rio(void *context,uint32_t offset,uint32_t *value)
{
    struct gpio_rio *gpio=context;
    switch (offset) {
    case 4+8*GPIO_PIN: *value=gpio->control; break;
    case 0x20004+4*GPIO_PIN: *value=gpio->pad; break;
    case 0x10000: *value=gpio->out; break;
    case 0x10004: *value=gpio->oe; break;
    case 0x10008: *value=0; break;
    default: assert(false);
    }
    return true;
}

static bool write_rio(void *context,uint32_t offset,uint32_t value)
{
    struct gpio_rio *gpio=context;
    assert(value==GPIO_MASK && gpio->control==31 && gpio->pad==0x84);
    if (++gpio->writes==gpio->drop_write) { return true; }
    switch (offset) {
    case 0x12000: gpio->out|=value; break;
    case 0x12004: gpio->oe|=value; break;
    case 0x13000: gpio->out&=~value; break;
    case 0x13004: gpio->oe&=~value; break;
    default: assert(false);
    }
    return true;
}

static void test_gpio_rio(void)
{
    const struct gpio_rio original={31,0x84,GPIO_MASK|OTHER_PIN,OTHER_PIN,0,0};
    struct gpio_rio gpio=original;
    const struct xhci_observer io={read_rio,&gpio,RP1_GPIO_SPAN};
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_OK && gpio.writes==6);
    assert(gpio.out==original.out && gpio.oe==original.oe);
    gpio=original;
    gpio.pad=4;
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_BUSY && !gpio.writes);
    gpio=original;
    gpio.control=5;
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_BUSY && !gpio.writes);
    gpio=original;
    gpio.oe|=GPIO_MASK;
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_BUSY && !gpio.writes);
    gpio=original;
    gpio.drop_write=3;
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_IO && gpio.writes==5);
    assert(gpio.out==original.out && gpio.oe==original.oe);
    gpio=original;
    gpio.drop_write=5;
    assert(rp1_gpio_test(&io,write_rio,GPIO_PIN)==RP1_GPIO_RESTORE && gpio.writes==6);
    assert(gpio.out==original.out && gpio.oe==(original.oe|GPIO_MASK));
}

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb");
    assert(file);
    static unsigned char dtb[128*1024];
    size_t bytes=fread(dtb,1,sizeof(dtb),file);
    assert(bytes>40 && feof(file) && !ferror(file));
    fclose(file);

    struct rp1_topology topology;
    test_gpio(dtb,bytes);
    test_gpio_rio();
    assert(rp1_pci_description(dtb,bytes) && !rp1_pci_description(dtb,bytes-1));
    assert(rp1_topology_read(dtb,bytes,&topology));
    assert(rp1_dma_description(dtb,bytes) && !rp1_dma_description(dtb,bytes-1));
    assert(!strcmp(topology.msi_parent,"/axi/msi-controller@1000130000"));
    assert(topology.routes[RP1_UART].line==25 && topology.routes[RP1_UART].trigger==4);
    assert(topology.routes[RP1_USB0].line==31 && topology.routes[RP1_USB0].trigger==1);
    assert(topology.routes[RP1_USB1].line==36 && topology.routes[RP1_USB1].trigger==1);
    assert(topology.routes[RP1_ETH].line==6 && topology.routes[RP1_ETH].trigger==4);
    assert(topology.routes[RP1_DMA].line==40 && topology.dma_channels==8);
    test_snapshot(&topology);
    test_mip(dtb,bytes,topology.msi_parent);
    assert(!rp1_topology_read(dtb,bytes-1,&topology));
    const unsigned char *p;
    uint32_t n;
    assert(dt_property(dtb,bytes,"/axi/pcie@1000120000/rp1/usb@300000","interrupts",&p,&n) && n==8);
    dtb[p-dtb+3]=64;
    assert(rp1_topology_read(dtb,bytes,&topology) && !topology.routes[RP1_USB1].trigger);
    assert(dt_property(dtb,bytes,"/axi/pcie@1000120000","msi-parent",&p,&n) && n==4);
    dtb[p-dtb]=0xff;
    assert(!rp1_topology_read(dtb,bytes,&topology));

    struct pi5_dma_window wrapping={.bus=UINT64_MAX-1,.size=4};
    uint64_t address=42;
    assert(!pi5_dma_address(&wrapping,0,4,&address) && address==42);
    puts("PASS: RP1 routes, MIP readback, GPIO bounds, isolated RIO test and restoration, DMA overflow refusal.");
    return 0;
}
