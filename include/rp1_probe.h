#ifndef OS_RP1_PROBE_H
#define OS_RP1_PROBE_H
#include "dt_property.h"
#include "usb.h"

#define RP1_PCIE_BASE 0x1000120000ULL
#define RP1_LIVE_BASE 0x1c00000000ULL
#define RP1_PCIE_APBS 0x108000
#define RP1_DMA_OFFSET 0x188000
#define RP1_GPIO_OFFSET 0xd0000
#define RP1_GPIO_BASE (RP1_LIVE_BASE+RP1_GPIO_OFFSET)
enum { RP1_HEADER_GPIOS=28, RP1_GPIO_SPAN=0x20074 };
struct rp1_gpio_snapshot { uint32_t control,pad; bool input,output,output_enable; };
enum rp1_gpio_result { RP1_GPIO_OK, RP1_GPIO_BUSY, RP1_GPIO_IO, RP1_GPIO_RESTORE };
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
bool rp1_topology_read(const void *,size_t,struct rp1_topology *);
bool rp1_dma_description(const void *,size_t);
bool rp1_gpio_description(const void *,size_t);
bool rp1_gpio_read(const struct xhci_observer *,unsigned,struct rp1_gpio_snapshot *);
enum rp1_gpio_result rp1_gpio_test(const struct xhci_observer *,
                                   bool (*write)(void *,uint32_t,uint32_t),unsigned);
bool rp1_mip_describe(const void *,size_t,const char *path,struct rp1_mip *);
bool rp1_mip_read(const struct xhci_observer *,struct rp1_mip_snapshot *);
enum rp1_snapshot_result rp1_reachable(const struct xhci_observer *,uint32_t offset);
enum rp1_snapshot_result rp1_irq_read(const struct xhci_observer *,const struct xhci_observer *,
                                      const struct rp1_topology *, struct rp1_irq_snapshot *);
bool rp1_command(const char *,uintptr_t dtb,uintptr_t uart);
bool rp1_gpio_command(const char *,uintptr_t dtb,uintptr_t uart);
#endif
