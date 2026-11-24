/* Cadence GEM Clause 22 management transactions for the onboard RP1 PHY. */
#include "rp1_probe.h"

enum { NET_CONTROL=0, NET_CONFIG=4, NET_STATUS=8, PHY_MAINTENANCE=0x34, MAC_ID=0xfc };
enum { MPE=1u<<4, MDIO_IDLE=1u<<2, MDC_DIV_224=7u<<18 };
enum {
    BMCR_RESTART=0x200, BMCR_AN_ENABLE=0x1000, BMCR_MODE_MASK=0xdc00,
    BMSR_LINK=4, BMSR_CAN_NEGOTIATE=8, BMSR_NEGOTIATED=0x20
};

static bool read_reg(const struct xhci_observer *io,uint32_t offset,uint32_t *value)
{
    return io->read32(io->context,offset,value) && *value!=UINT32_MAX;
}

static bool idle(const struct xhci_observer *io,uint64_t (*ticks)(void *),uint64_t hz)
{
    uint64_t start=ticks(io->context);
    for (unsigned spin=0;spin<100000;++spin) {
        uint32_t status;
        if (!read_reg(io,NET_STATUS,&status)) { return false; }
        if (status&MDIO_IDLE) { return true; }
        if (ticks(io->context)-start>=hz/100) { break; }
    }
    return false;
}

static bool read_clause22(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                          uint64_t (*ticks)(void *),uint64_t hz,unsigned reg,uint32_t *value)
{
    /* PHY address 1, read opcode, turnaround 10. */
    return write(io->context,PHY_MAINTENANCE,0x60820000u|reg<<18) &&
           idle(io,ticks,hz) && read_reg(io,PHY_MAINTENANCE,value);
}

static enum rp1_phy_result recover_link(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                        uint64_t (*ticks)(void *),uint64_t hz,struct rp1_phy_snapshot *out)
{
    const unsigned ready=BMSR_LINK|BMSR_NEGOTIATED;
    if ((out->status&ready)==ready) { return RP1_PHY_OK; }
    if (out->phy_id!=0x600d84a2 || (out->control&BMCR_MODE_MASK)!=BMCR_AN_ENABLE ||
        !(out->status&BMSR_CAN_NEGOTIATE)) { return RP1_PHY_MODE; }

    /* BMCR restart is self-clearing. Preserve the other control bits. */
    if (!write(io->context,PHY_MAINTENANCE,0x50820000u|out->control|BMCR_RESTART) ||
        !idle(io,ticks,hz)) { return RP1_PHY_IO; }
    out->restarted=true;
    for (unsigned poll=0;poll<40;++poll) {
        uint64_t start=ticks(io->context);
        unsigned spin=0;
        while (ticks(io->context)-start<hz/10) {
            if (++spin==100000000) { return RP1_PHY_IO; }
        }
        uint32_t value;
        if (!read_clause22(io,write,ticks,hz,1,&value) ||
            !read_clause22(io,write,ticks,hz,1,&value)) { return RP1_PHY_IO; }
        out->status=(uint16_t)value;
        if (!out->status || out->status==UINT16_MAX) { return RP1_PHY_NO_REPLY; }
        if ((out->status&ready)==ready) { break; }
    }
    return RP1_PHY_OK;
}

static enum rp1_phy_result read_phy(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                    uint64_t (*ticks)(void *),uint64_t hz,struct rp1_phy_snapshot *out,
                                    enum rp1_eth_action action)
{
    uint32_t control,config,value;
    if (!io || !io->read32 || io->span<MAC_ID+4 || !write || !ticks || hz<100 || !out) { return RP1_PHY_MAC; }
    *out=(struct rp1_phy_snapshot){0};
    if (!read_reg(io,MAC_ID,&out->mac_id) || ((out->mac_id>>16)&0xfff)<2 ||
        !read_reg(io,NET_CONTROL,&control) || !read_reg(io,NET_CONFIG,&config)) { return RP1_PHY_MAC; }
    if (control || !idle(io,ticks,hz)) { return RP1_PHY_BUSY; }

    enum rp1_phy_result result=RP1_PHY_IO;
    if (write(io->context,NET_CONFIG,config|MDC_DIV_224) && read_reg(io,NET_CONFIG,&value) &&
        value==(config|MDC_DIV_224) && write(io->context,NET_CONTROL,MPE) && read_reg(io,NET_CONTROL,&value) && value==MPE) {
        /* Read status twice to clear the latched link-down indication. */
        static const uint8_t registers[]={2,3,0,1,1,0x19};
        uint16_t data[sizeof(registers)];
        bool resolve=action==ETH_ARP || action==ETH_PING || action==ETH_UDP_UP || action==ETH_TCP_UP;
        unsigned i=0,count=resolve ? sizeof(registers) : sizeof(registers)-1;
        for (;i<count;++i) {
            if (!read_clause22(io,write,ticks,hz,registers[i],&value)) { break; }
            data[i]=(uint16_t)value;
        }
        if (i==count) {
            out->phy_id=(uint32_t)data[0]<<16|data[1];
            out->control=data[2]; out->status=data[4];
            if (resolve) { out->aux=data[5]; }
            result=out->phy_id && out->phy_id!=UINT32_MAX && out->control!=UINT16_MAX &&
                   out->status && out->status!=UINT16_MAX ? RP1_PHY_OK : RP1_PHY_NO_REPLY;
            if (result==RP1_PHY_OK && action==ETH_RECOVER) { result=recover_link(io,write,ticks,hz,out); }
        }
    }
    /* Disable the management port before restoring the original divider. */
    bool settled=idle(io,ticks,hz);
    bool disabled=write(io->context,NET_CONTROL,control) && read_reg(io,NET_CONTROL,&value) && value==control;
    bool restored=disabled && write(io->context,NET_CONFIG,config) && read_reg(io,NET_CONFIG,&value) && value==config;
    return settled && restored ? result : RP1_PHY_RESTORE;
}

enum rp1_phy_result rp1_phy_read(const struct xhci_observer *io,bool (*write)(void *,uint32_t,uint32_t),
                                uint64_t (*ticks)(void *),uint64_t hz,struct rp1_phy_snapshot *out)
{
    return read_phy(io,write,ticks,hz,out,ETH_PROBE);
}

unsigned rp1_phy_full_duplex_speed(const struct rp1_phy_snapshot *phy)
{
    /* BCM54213PE AUX status HCD, with completed Clause 22 negotiation. */
    if (phy->phy_id!=0x600d84a2 || !(phy->control&0x1000) ||
        (phy->status&0x34)!=0x24 || (phy->aux&0x80c4)!=0x8004) { return 0; }
    switch (phy->aux&0x700) {
    case 0x700: return 1000;
    case 0x500: return 100;
    case 0x200: return 10;
    default: return 0;
    }
}

enum rp1_phy_result rp1_eth_link_control(const struct rp1_eth_io *io,enum rp1_eth_action action,
                                        struct rp1_eth_link *link,struct rp1_phy_snapshot *out)
{
    if (action==ETH_DOWN) {
        if (link->held && !rp1_eth_reset_restore(&io->gpio,io->write,&link->saved)) { return RP1_PHY_RESTORE; }
        link->held=false;
        return RP1_PHY_OK;
    }

    if ((action==ETH_ARP || action==ETH_PING || action==ETH_UDP_UP ||
         action==ETH_TCP_UP || action==ETH_RECOVER) && !link->held) {
        return RP1_PHY_BUSY;
    }
    enum rp1_phy_result result=read_phy(&io->gem,io->write,io->ticks,io->hz,out,action);
    bool pulse=(action==ETH_PULSE && result==RP1_PHY_NO_REPLY) ||
               (action==ETH_UP && (result==RP1_PHY_OK || result==RP1_PHY_NO_REPLY));
    if (link->held || !pulse) { return result; }

    struct rp1_gpio_snapshot saved;
    enum rp1_gpio_result reset=rp1_eth_reset(&io->gpio,io->write,io->ticks,io->hz,&saved);
    if (reset!=RP1_GPIO_OK) { return reset==RP1_GPIO_RESTORE ? RP1_PHY_RESTORE : RP1_PHY_PIN; }
    result=rp1_phy_read(&io->gem,io->write,io->ticks,io->hz,out);
    out->pulsed=true;
    if (action==ETH_UP && result==RP1_PHY_OK) {
        link->saved=saved;
        link->held=true;
    } else if (!rp1_eth_reset_restore(&io->gpio,io->write,&saved)) {
        result=RP1_PHY_RESTORE;
    }
    return result;
}
