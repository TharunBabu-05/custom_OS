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

enum { LOOP_TX_MASK=1u<<RP1_LOOP_TX, LOOP_RX_MASK=1u<<RP1_LOOP_RX };
struct gpio_loop {
    uint32_t out,oe,tx_control,rx_control,tx_pad,rx_pad,voltage;
    unsigned writes,drop_write,samples,pattern;
    bool connected,tx_pad_stuck,rx_rio_stuck;
};

static bool driving(const struct gpio_loop *loop)
{
    return (loop->tx_control&31)==5 && !(loop->tx_pad&0x80) && (loop->oe&LOOP_TX_MASK);
}

static bool read_loop(void *context,uint32_t offset,uint32_t *value)
{
    struct gpio_loop *loop=context;
    bool active=driving(loop),level=(loop->out&LOOP_TX_MASK)!=0;
    bool tx=(loop->tx_pad&0x40) && !loop->tx_pad_stuck && (active ? level : (loop->tx_pad&8)!=0);
    bool rx=(loop->rx_pad&0x40) && (active && loop->connected ? level : (loop->rx_pad&8)!=0);
    switch (offset) {
    case 8*RP1_LOOP_TX: *value=(tx ? 1u<<17 : 0)|(active ? 1u<<13 : 0)|(level ? 1u<<9 : 0); break;
    case 8*RP1_LOOP_RX: *value=rx ? 1u<<17 : 0; break;
    case 4+8*RP1_LOOP_TX: *value=loop->tx_control; break;
    case 4+8*RP1_LOOP_RX: *value=loop->rx_control; break;
    case 0x20004+4*RP1_LOOP_TX: *value=loop->tx_pad; break;
    case 0x20004+4*RP1_LOOP_RX: *value=loop->rx_pad; break;
    case 0x20000: *value=loop->voltage; break;
    case 0x10000: *value=loop->out; break;
    case 0x10004: *value=loop->oe; break;
    case 0x10008: *value=(tx ? LOOP_TX_MASK : 0)|(rx && !loop->rx_rio_stuck ? LOOP_RX_MASK : 0); break;
    default: assert(false);
    }
    return true;
}

static bool write_loop(void *context,uint32_t offset,uint32_t value)
{
    struct gpio_loop *loop=context;
    if (++loop->writes==loop->drop_write) { return true; }
    switch (offset) {
    case 4+8*RP1_LOOP_TX: loop->tx_control=value; break;
    case 4+8*RP1_LOOP_RX: loop->rx_control=value; break;
    case 0x20004+4*RP1_LOOP_TX: loop->tx_pad=value; break;
    case 0x20004+4*RP1_LOOP_RX: loop->rx_pad=value; break;
    case 0x12000: case 0x13000:
        assert(value==LOOP_TX_MASK);
        if (offset==0x12000) { loop->out|=value; }
        else { loop->out&=~value; }
        if (driving(loop)) {
            loop->pattern=2*loop->pattern+(offset==0x12000);
            ++loop->samples;
        }
        break;
    case 0x12004: assert(value==LOOP_TX_MASK); loop->oe|=value; break;
    case 0x13004: assert(value==LOOP_TX_MASK); loop->oe&=~value; break;
    default: assert(false);
    }
    if (driving(loop)) {
        assert((loop->rx_pad&0xcc)==0xc4 && !(loop->oe&LOOP_RX_MASK));
        assert((loop->rx_control&31)==5 && !(loop->tx_pad&0x30));
    }
    return true;
}

static void test_gpio_loop(void)
{
    const struct gpio_loop original={.out=LOOP_TX_MASK|OTHER_PIN,.oe=OTHER_PIN,
        .tx_control=31,.rx_control=31,.tx_pad=0x94,.rx_pad=0x84,.connected=true};
    struct gpio_loop loop=original;
    const struct xhci_observer io={read_loop,&loop,RP1_GPIO_SPAN};
    struct rp1_gpio_sample sample;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_OK && loop.samples==5 && loop.pattern==10);
    assert(loop.writes==18 && loop.out==original.out && loop.oe==original.oe);
    assert(loop.tx_control==31 && loop.rx_control==31 && loop.tx_pad==0x94 && loop.rx_pad==0x84);
    loop=original;
    loop.connected=false;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_SIGNAL && loop.samples==2);
    assert(sample.step==1 && sample.pin==RP1_LOOP_RX && sample.expected && !sample.pad && !sample.rio);
    assert(loop.out==original.out && loop.oe==original.oe);
    assert(loop.tx_control==31 && loop.rx_control==31 && loop.tx_pad==0x94 && loop.rx_pad==0x84);
    loop=original;
    loop.tx_pad_stuck=true;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_SIGNAL);
    assert(sample.step==1 && sample.pin==RP1_LOOP_TX && sample.expected && !sample.pad && !sample.rio);
    assert(sample.status==((1u<<13)|(1u<<9)) && !driving(&loop));
    loop=original;
    loop.rx_rio_stuck=true;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_SIGNAL);
    assert(sample.step==1 && sample.pin==RP1_LOOP_RX && sample.expected && sample.pad && !sample.rio);
    loop=original;
    loop.voltage=1;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_BUSY && !loop.writes);
    loop=original;
    loop.rx_pad=4;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_BUSY && !loop.writes);
    loop=original;
    loop.drop_write=2;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_IO && !loop.samples);
    assert(loop.out==original.out && loop.oe==original.oe && loop.rx_pad==0x84);
    loop=original;
    loop.drop_write=17;
    assert(rp1_gpio_loop(&io,write_loop,&sample)==RP1_GPIO_RESTORE && loop.writes==18);
    assert(!driving(&loop) && loop.tx_pad==0x94 && loop.rx_pad==0x84);

    loop=original;
    assert(rp1_gpio_pull(&io,write_loop,RP1_LOOP_RX,&sample)==RP1_GPIO_OK && loop.writes==6);
    assert(loop.rx_control==31 && loop.rx_pad==0x84 && loop.oe==original.oe && loop.out==original.out);
    loop=original;
    loop.rx_rio_stuck=true;
    assert(rp1_gpio_pull(&io,write_loop,RP1_LOOP_RX,&sample)==RP1_GPIO_SIGNAL);
    assert(sample.pin==RP1_LOOP_RX && sample.step==1 && sample.pad && !sample.rio && loop.rx_control==31);
    loop=original;
    loop.rx_control|=1u<<23;
    assert(rp1_gpio_pull(&io,write_loop,RP1_LOOP_RX,&sample)==RP1_GPIO_BUSY && !loop.writes);
    loop=original;
    loop.drop_write=6;
    assert(rp1_gpio_pull(&io,write_loop,RP1_LOOP_RX,&sample)==RP1_GPIO_RESTORE);
    assert((loop.rx_pad&0x80) && loop.oe==original.oe && loop.out==original.out);
}

struct phy_model {
    uint32_t control,config,request,mac_id;
    unsigned writes,reads,delay,restarts,recovery_reads,link_after;
    uint64_t time;
    uint16_t phy_control;
    bool no_reply,stuck,drop_restore,link,drop_restart,lose_phy;
};

static bool phy_read_reg(void *context,uint32_t offset,uint32_t *out)
{
    struct phy_model *m=context;
    switch (offset) {
    case 0: *out=m->control; return true;
    case 4: *out=m->config; return true;
    case 8:
        *out=m->delay ? 0 : 4;
        if (m->delay && !m->stuck) { --m->delay; }
        return true;
    case 0x34: *out=m->request; return true;
    case 0xfc: *out=m->mac_id; return true;
    default: assert(!"unexpected GEM register read"); return false;
    }
}

static bool phy_write_reg(void *context,uint32_t offset,uint32_t value)
{
    static const uint8_t regs[]={2,3,0,1,1,0x19};
    static const uint16_t replies[]={0x600d,0x84a2,0x1140,0x7949,0x796d,0x8504};
    struct phy_model *m=context;
    ++m->writes;
    if (offset==0) {
        assert(value==0 || value==0x10);
        m->control=value;
    } else if (offset==4) {
        assert(!m->control && (value==0x1200 || value==(0x1200|7u<<18)));
        if (!m->drop_restore || value!=0x1200) { m->config=value; }
    } else {
        assert(offset==0x34 && m->control==0x10 && !m->delay);
        if (value==0x50821340u) {
            assert(m->reads==5 && !m->link && !m->phy_control);
            if (m->drop_restart) { return false; }
            ++m->restarts;
            m->recovery_reads=0;
            m->delay=2;
            return true;
        }
        if (m->restarts && m->reads==5) {
            assert(value==0x60860000u);
            ++m->recovery_reads;
            bool linked=m->link_after && m->recovery_reads>=m->link_after && !(m->recovery_reads&1);
            m->request=value|(m->lose_phy ? UINT16_MAX : linked ? 0x796d : 0x7949);
            m->delay=2;
            return true;
        }
        assert(m->reads<sizeof(regs));
        assert(value==(0x60820000u|(uint32_t)regs[m->reads]<<18));
        uint16_t reply=replies[m->reads++];
        if (m->phy_control && m->reads==3) { reply=m->phy_control; }
        if (!m->link && m->reads==5) { reply=0x7949; }
        m->request=value|(m->no_reply ? UINT16_MAX : reply);
        m->delay=2;
    }
    return true;
}

static uint64_t phy_time(void *context) { return ++((struct phy_model *)context)->time; }

enum { RESET_PIN=1u<<4, NEIGHBOR=1u<<3 };
struct reset_model {
    uint32_t control,pad,out,oe;
    uint64_t time,asserted_at,released_at;
    unsigned writes,fail_write;
};

static bool reset_read(void *context,uint32_t offset,uint32_t *out)
{
    struct reset_model *m=context;
    bool driven=m->control==5 && !(m->pad&0x80) && (m->oe&RESET_PIN);
    bool high=driven && (m->out&RESET_PIN);
    switch (offset) {
    case 0x20: *out=(driven ? 1u<<13 : 0)|(high ? (1u<<17)|(1u<<9) : 0); return true;
    case 0x24: *out=m->control; return true;
    case 0x20014: *out=m->pad; return true;
    case 0x10000: *out=m->out; return true;
    case 0x10004: *out=m->oe; return true;
    case 0x10008: *out=high ? RESET_PIN : 0; return true;
    default: assert(!"unexpected PHY reset register read"); return false;
    }
}

static bool reset_write(void *context,uint32_t offset,uint32_t value)
{
    struct reset_model *m=context;
    if (++m->writes==m->fail_write) { return false; }
    if (offset==0x24) { assert(m->pad&0x80); m->control=value; return true; }
    if (offset==0x20014) {
        m->pad=value;
        if (!(value&0x80) && !(m->out&RESET_PIN)) { m->asserted_at=m->time; }
        return true;
    }
    assert(value==RESET_PIN);
    switch (offset) {
    case 0x12000: m->out|=value; m->released_at=m->time; return true;
    case 0x13000: m->out&=~value; return true;
    case 0x12004: assert(m->pad&0x80); m->oe|=value; return true;
    case 0x13004: assert(m->pad&0x80); m->oe&=~value; return true;
    default: assert(!"unexpected PHY reset register write"); return false;
    }
}

static uint64_t reset_time(void *context) { return ++((struct reset_model *)context)->time; }

static void test_ethernet_reset(unsigned char *dtb,size_t bytes)
{
    assert(rp1_eth_reset_description(dtb,bytes) && !rp1_eth_reset_description(dtb,bytes-1));
    const unsigned char *p; uint32_t n;
    assert(dt_property(dtb,bytes,"/axi/pcie@1000120000/rp1/ethernet@100000","phy-reset-gpios",&p,&n) && n==12);
    dtb[p-dtb+11]^=1;
    assert(!rp1_eth_reset_description(dtb,bytes));
    dtb[p-dtb+11]^=1;

    const struct reset_model original={.control=31,.pad=0x84,.out=NEIGHBOR,.oe=NEIGHBOR};
    struct reset_model m=original;
    const struct xhci_observer io={reset_read,&m,RP1_ETH_RESET_SPAN};
    struct rp1_gpio_snapshot saved;
    assert(rp1_eth_reset(&io,reset_write,reset_time,1000,&saved)==RP1_GPIO_OK);
    assert(m.writes==7 && m.released_at-m.asserted_at>=5 && m.time-m.released_at>=100);
    assert(m.out==(RESET_PIN|NEIGHBOR) && m.oe==(RESET_PIN|NEIGHBOR) && m.control==5 && m.pad==0x44);
    assert(rp1_eth_reset_restore(&io,reset_write,&saved));
    for (unsigned fail=0;fail<=7;++fail) {
        if (fail) {
            m=original; m.fail_write=fail;
            assert(rp1_eth_reset(&io,reset_write,reset_time,1000,&saved)==RP1_GPIO_IO);
        }
        assert(m.control==original.control && m.pad==original.pad && m.out==original.out && m.oe==original.oe);
    }
    m=original; m.control=1;
    assert(rp1_eth_reset(&io,reset_write,reset_time,1000,&saved)==RP1_GPIO_BUSY && !m.writes);
    m=original;
    assert(rp1_eth_reset(&io,reset_write,reset_time,1000,&saved)==RP1_GPIO_OK);
    m.fail_write=m.writes+3;
    assert(!rp1_eth_reset_restore(&io,reset_write,&saved) && m.out==(NEIGHBOR|RESET_PIN));
    puts("PASS: Pi PHY reset DT polarity, timed low/high pin readback, neighbor isolation, setup failures and restore failure detection.");
}

struct eth_model { struct phy_model gem; struct reset_model gpio; bool phy_missing; };
struct eth_port { struct eth_model *model; bool gpio; };

static bool link_read(void *context,uint32_t offset,uint32_t *value)
{
    struct eth_port *port=context;
    return port->gpio ? reset_read(&port->model->gpio,offset,value) : phy_read_reg(&port->model->gem,offset,value);
}

static bool link_write(void *context,uint32_t offset,uint32_t value)
{
    struct eth_port *port=context;
    struct eth_model *m=port->model;
    if (port->gpio) { return reset_write(&m->gpio,offset,value); }
    if (offset==0 && value==0x10) {
        m->gem.reads=0;
        m->gem.no_reply=m->phy_missing || m->gpio.control!=5 || (m->gpio.pad&0x80) ||
            !(m->gpio.oe&RESET_PIN) || !(m->gpio.out&RESET_PIN);
    }
    return phy_write_reg(&m->gem,offset,value);
}

static uint64_t link_time(void *context)
{
    struct eth_port *port=context;
    return port->gpio ? reset_time(&port->model->gpio) : phy_time(&port->model->gem);
}

static void test_ethernet_link(void)
{
    const struct eth_model initial={
        .gem={.config=0x1200,.mac_id=0x00070109},.gpio={.control=31,.pad=0x84,.out=NEIGHBOR,.oe=NEIGHBOR}
    };
    struct eth_model model=initial;
    struct eth_port gem={&model,false},gpio={&model,true};
    const struct rp1_eth_io io={
        .gem={link_read,&gem,RP1_ETH_SPAN},.gpio={link_read,&gpio,RP1_ETH_RESET_SPAN},
        .write=link_write,.ticks=link_time,.hz=1000
    };
    struct rp1_eth_link link={0};
    struct rp1_phy_snapshot snapshot;
    assert(rp1_eth_link_control(&io,ETH_ARP,&link,&snapshot)==RP1_PHY_BUSY && !model.gem.writes);
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_BUSY && !model.gem.writes);
    assert(rp1_eth_link_control(&io,ETH_PROBE,&link,&snapshot)==RP1_PHY_NO_REPLY && !model.gpio.writes);
    assert(rp1_eth_link_control(&io,ETH_PULSE,&link,&snapshot)==RP1_PHY_OK && snapshot.pulsed && !link.held);
    assert(rp1_eth_link_control(&io,ETH_PROBE,&link,&snapshot)==RP1_PHY_NO_REPLY);
    assert(rp1_eth_link_control(&io,ETH_UP,&link,&snapshot)==RP1_PHY_OK && snapshot.pulsed && link.held);
    assert(!(snapshot.status&4) && snapshot.phy_id==0x600d84a2);
    unsigned writes=model.gpio.writes;
    model.gem.link=true;
    assert(rp1_eth_link_control(&io,ETH_PROBE,&link,&snapshot)==RP1_PHY_OK && (snapshot.status&4) && !snapshot.pulsed);
    assert(rp1_eth_link_control(&io,ETH_UP,&link,&snapshot)==RP1_PHY_OK && link.held && model.gpio.writes==writes);
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_OK &&
           !snapshot.restarted && !model.gem.restarts);
    assert(rp1_eth_link_control(&io,ETH_ARP,&link,&snapshot)==RP1_PHY_OK &&
           rp1_phy_full_duplex_speed(&snapshot)==100 && model.gem.reads==6);
    snapshot.aux=0x8704;
    assert(rp1_phy_full_duplex_speed(&snapshot)==1000);
    snapshot.aux=0x8204;
    assert(rp1_phy_full_duplex_speed(&snapshot)==10);
    snapshot.aux=0x8304;
    assert(!rp1_phy_full_duplex_speed(&snapshot));
    snapshot.aux=0x8504;
    snapshot.status&=~4u;
    assert(!rp1_phy_full_duplex_speed(&snapshot));
    model.gem.link=false;
    model.gem.phy_control=0x1940; /* Power-down mode must remain untouched. */
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_MODE && !model.gem.restarts);
    model.gem.phy_control=0;
    model.gem.drop_restart=true;
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_IO && !snapshot.restarted);
    assert(!model.gem.control && model.gem.config==initial.gem.config);
    model.gem.drop_restart=false;
    uint64_t before=model.gem.time;
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_OK && snapshot.restarted);
    assert(snapshot.status==0x7949 && model.gem.restarts==1 && model.gem.recovery_reads==80);
    assert(model.gem.time-before>=4000 && model.gem.time-before<5000);
    model.gem.restarts=0;
    model.gem.link_after=3;
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_OK && snapshot.restarted);
    assert(snapshot.status==0x796d && model.gem.restarts==1 && model.gem.recovery_reads==4);
    model.gem.restarts=0;
    model.gem.lose_phy=true;
    assert(rp1_eth_link_control(&io,ETH_RECOVER,&link,&snapshot)==RP1_PHY_NO_REPLY && snapshot.restarted);
    assert(link.held && model.gem.restarts==1 && !model.gem.control && model.gem.config==initial.gem.config);
    assert(model.gpio.writes==writes);
    assert(rp1_eth_link_control(&io,ETH_DOWN,&link,&snapshot)==RP1_PHY_OK && !link.held);
    assert(model.gpio.control==initial.gpio.control && model.gpio.pad==initial.gpio.pad &&
           model.gpio.out==initial.gpio.out && model.gpio.oe==initial.gpio.oe);
    writes=model.gpio.writes;
    assert(rp1_eth_link_control(&io,ETH_DOWN,&link,&snapshot)==RP1_PHY_OK && model.gpio.writes==writes);

    model=initial; model.phy_missing=true;
    assert(rp1_eth_link_control(&io,ETH_UP,&link,&snapshot)==RP1_PHY_NO_REPLY && snapshot.pulsed && !link.held);
    assert(model.gpio.control==initial.gpio.control && model.gpio.pad==initial.gpio.pad && model.gpio.oe==initial.gpio.oe);
    model=initial;
    assert(rp1_eth_link_control(&io,ETH_UP,&link,&snapshot)==RP1_PHY_OK && link.held);
    model.gpio.fail_write=model.gpio.writes+3;
    assert(rp1_eth_link_control(&io,ETH_DOWN,&link,&snapshot)==RP1_PHY_RESTORE && link.held);
    puts("PASS: pulse-only vs sustained PHY, link change while held, repeat-up idempotence, cleanup on no reply and failed release detection.");
    puts("PASS: one Clause 22 negotiation restart on held PHY, live-link and unsafe-mode skips, latched status, timeout and management restore.");
}

static void test_ethernet(unsigned char *dtb,size_t bytes)
{
    assert(rp1_eth_description(dtb,bytes) && !rp1_eth_description(dtb,bytes-1));
    const char *node="/axi/pcie@1000120000/rp1/ethernet@100000";
    const char *const properties[]={"reg","compatible","phy-mode","phy-handle","clocks","status"};
    for (unsigned i=0;i<sizeof(properties)/sizeof(*properties);++i) {
        const unsigned char *p; uint32_t n;
        assert(dt_property(dtb,bytes,node,properties[i],&p,&n) && n);
        unsigned index=i==4 ? 7 : n-1;
        dtb[p-dtb+index]^=1;
        assert(!rp1_eth_description(dtb,bytes));
        dtb[p-dtb+index]^=1;
    }

    const struct phy_model initial={.config=0x1200,.mac_id=0x00070132,.link=true};
    struct phy_model model=initial;
    const struct xhci_observer io={phy_read_reg,&model,RP1_ETH_SPAN};
    struct rp1_phy_snapshot snapshot;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_OK);
    assert(snapshot.mac_id==initial.mac_id && snapshot.phy_id==0x600d84a2 && snapshot.status==0x796d && snapshot.control==0x1140);
    assert(model.writes==9 && model.reads==5 && model.config==initial.config && !model.control);
    model=initial; model.link=false;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_OK && !(snapshot.status&4));
    model=initial; model.no_reply=true;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_NO_REPLY && model.config==initial.config && !model.control);
    model=initial; model.control=8;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_BUSY && !model.writes);
    model=initial; model.mac_id=UINT32_MAX;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_MAC && !model.writes);
    model=initial; model.stuck=true;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_RESTORE && model.reads==1 && !model.control);
    model=initial; model.drop_restore=true;
    assert(rp1_phy_read(&io,phy_write_reg,phy_time,1000,&snapshot)==RP1_PHY_RESTORE && model.reads==5);
    puts("PASS: Pi Ethernet binding, exact Clause 22 reads, latched link status, PHY absence, timeout, busy refusal and verified MAC restore.");
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
    test_gpio_loop();
    test_ethernet(dtb,bytes);
    test_ethernet_reset(dtb,bytes);
    test_ethernet_link();
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
    puts("PASS: RP1 routes, MIP, GPIO bounds, RIO, pad and loopback diagnostics and restoration, DMA overflow refusal.");
    return 0;
}
