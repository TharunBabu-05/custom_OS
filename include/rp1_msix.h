#ifndef OS_RP1_MSIX_H
#define OS_RP1_MSIX_H
#include "usb.h"

struct rp1_msix {
    uint64_t table_bus,pba_bus,peripheral_bus;
    uint16_t control,entries;
    uint8_t capability,bus;
};
struct rp1_pci_header { uint32_t link,buses,header; };
enum rp1_pci_result {
    RP1_PCI_OK, RP1_PCI_BRIDGE, RP1_PCI_LINK, RP1_PCI_BUS, RP1_PCI_SELECTOR, RP1_PCI_ENDPOINT,
    RP1_PCI_CAPABILITY, RP1_PCI_RESTORE
};
typedef bool (*rp1_pci_write)(void *,uint32_t,uint32_t);
bool rp1_pci_description(const void *,size_t);
enum rp1_pci_result rp1_msix_read(const struct xhci_observer *,rp1_pci_write,
                                  struct rp1_pci_header *,struct rp1_msix *);
bool rp1_pci_cpu_address(const struct xhci_observer *,uint64_t bus,uint32_t bytes,uint64_t *cpu);
enum rp1_mip_aperture { RP1_MIP_UNAVAILABLE, RP1_MIP_UNMAPPED, RP1_MIP_MAPPED };
enum rp1_mip_aperture rp1_mip_aperture_read(const struct xhci_observer *);
#endif
