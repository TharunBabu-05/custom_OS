#ifndef OS_RP1_PROBE_H
#define OS_RP1_PROBE_H
#include "dt_property.h"
#include "usb.h"

#define RP1_PCIE_BASE 0x1000120000ULL
#define RP1_LIVE_BASE 0x1c00000000ULL
#define RP1_PCIE_APBS 0x108000
#define RP1_DMA_OFFSET 0x188000
#define RP1_ETH_OFFSET 0x100000
#define RP1_ETH_SPAN 0x4000
#define RP1_GPIO_OFFSET 0xd0000
#define RP1_GPIO_BASE (RP1_LIVE_BASE+RP1_GPIO_OFFSET)
#define RP1_ETH_RESET_GPIO 32
#define RP1_ETH_RESET_BANK 0x4000
#define RP1_ETH_RESET_SPAN 0x20018
enum { RP1_HEADER_GPIOS=28, RP1_GPIO_SPAN=0x20074, RP1_LOOP_TX=17, RP1_LOOP_RX=27 };
struct rp1_gpio_snapshot { uint32_t control,pad; bool input,output,output_enable; };
struct rp1_gpio_sample { unsigned step,pin; uint32_t status; bool expected,pad,rio; };
enum rp1_gpio_result { RP1_GPIO_OK, RP1_GPIO_BUSY, RP1_GPIO_IO, RP1_GPIO_RESTORE, RP1_GPIO_SIGNAL };
enum { RP1_UART, RP1_USB0, RP1_USB1, RP1_ETH, RP1_DMA, RP1_ROUTE_COUNT };
struct rp1_route { uint8_t line,trigger; };
struct rp1_topology {
    struct rp1_route routes[RP1_ROUTE_COUNT];
    char msi_parent[DT_PATH_CAPACITY];
    unsigned dma_channels;
};
struct rp1_irq_snapshot {
    uint64_t raw;
    uint32_t config[RP1_ROUTE_COUNT];
};
struct rp1_mip {
    uint64_t doorbell;
    unsigned first_intid,vectors,data_offset;
};
struct rp1_mip_snapshot { uint64_t edge,host_mask,vpu_mask,host_pending; };
enum rp1_snapshot_result { RP1_SNAPSHOT_OK, RP1_SNAPSHOT_LINK, RP1_SNAPSHOT_WINDOW, RP1_SNAPSHOT_IO };
enum rp1_phy_result { RP1_PHY_OK,RP1_PHY_MAC,RP1_PHY_BUSY,RP1_PHY_IO,RP1_PHY_NO_REPLY,RP1_PHY_RESTORE,RP1_PHY_PIN,RP1_PHY_MODE };
enum rp1_eth_action { ETH_PROBE,ETH_PULSE,ETH_UP,ETH_DOWN,ETH_ARP,ETH_LOOP,ETH_RECOVER,ETH_PING,ETH_UDP_UP,ETH_TCP_UP };
enum { RP1_GEM_RX_TX=12,RP1_GEM_MPE=1u<<4,RP1_GEM_MDC_DIV_224=7u<<18 };
struct rp1_phy_snapshot { uint32_t mac_id,phy_id; uint16_t control,status,aux; bool pulsed,restarted; };
unsigned rp1_phy_full_duplex_speed(const struct rp1_phy_snapshot *);
struct rp1_eth_io {
    struct xhci_observer gem,gpio;
    bool (*write)(void *,uint32_t,uint32_t);
    uint64_t (*ticks)(void *);
    uint64_t hz;
};
struct rp1_eth_link { struct rp1_gpio_snapshot saved; bool held; };
bool rp1_eth_description(const void *,size_t);
bool rp1_eth_reset_description(const void *,size_t);
enum rp1_gpio_result rp1_eth_reset(const struct xhci_observer *,bool (*write)(void *,uint32_t,uint32_t),
                                   uint64_t (*ticks)(void *),uint64_t hz,struct rp1_gpio_snapshot *);
bool rp1_eth_reset_restore(const struct xhci_observer *,bool (*write)(void *,uint32_t,uint32_t),
                           const struct rp1_gpio_snapshot *);
enum rp1_phy_result rp1_phy_read(const struct xhci_observer *,bool (*write)(void *,uint32_t,uint32_t),
                                uint64_t (*ticks)(void *),uint64_t hz,struct rp1_phy_snapshot *);
enum rp1_phy_result rp1_phy_sample(const struct rp1_eth_io *,bool dma_active,struct rp1_phy_snapshot *);
enum rp1_phy_result rp1_eth_link_control(const struct rp1_eth_io *,enum rp1_eth_action,
                                        struct rp1_eth_link *,struct rp1_phy_snapshot *);
bool rp1_topology_read(const void *,size_t,struct rp1_topology *);
bool rp1_dma_description(const void *,size_t);
bool rp1_gpio_description(const void *,size_t);
bool rp1_gpio_read(const struct xhci_observer *,unsigned,struct rp1_gpio_snapshot *);
enum rp1_gpio_result rp1_gpio_test(const struct xhci_observer *,
                                   bool (*write)(void *,uint32_t,uint32_t),unsigned);
enum rp1_gpio_result rp1_gpio_loop(const struct xhci_observer *,bool (*write)(void *,uint32_t,uint32_t),
                                   struct rp1_gpio_sample *);
enum rp1_gpio_result rp1_gpio_pull(const struct xhci_observer *,bool (*write)(void *,uint32_t,uint32_t),unsigned,
                                   struct rp1_gpio_sample *);
bool rp1_mip_describe(const void *,size_t,const char *path,struct rp1_mip *);
bool rp1_mip_read(const struct xhci_observer *,struct rp1_mip_snapshot *);
enum rp1_snapshot_result rp1_reachable(const struct xhci_observer *,uint32_t offset);
enum rp1_snapshot_result rp1_irq_read(const struct xhci_observer *,const struct xhci_observer *,
                                      const struct rp1_topology *, struct rp1_irq_snapshot *);
bool rp1_command(const char *,uintptr_t dtb,uintptr_t uart);
void rp1_ethernet_command(uintptr_t dtb,uintptr_t uart,enum rp1_eth_action);
bool rp1_ethernet_address_command(const char *);
bool rp1_ethernet_active(void);
void rp1_ethernet_status(void);
void rp1_ethernet_stats(void);
bool rp1_gpio_command(const char *,uintptr_t dtb,uintptr_t uart);
#endif
