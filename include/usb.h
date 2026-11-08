#ifndef OS_USB_H
#define OS_USB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define USB_MAX_PORTS 32
/* Observation only: deliberately no register-write callback or DMA buffers. */
struct xhci_observer {
    bool (*read32)(void *context,uint32_t offset,uint32_t *value);
    void *context;
    uint32_t span;
};
struct usb_port_snapshot { uint32_t status; uint8_t protocol_major,protocol_minor; };
struct xhci_snapshot {
    uint16_t version,interrupters;
    uint8_t slots,ports,cap_length;
    bool address64,context64;
    uint32_t command,status,page_sizes;
    struct usb_port_snapshot port[USB_MAX_PORTS];
};
enum usb_probe_result { USB_PROBE_OK,USB_PROBE_ARGUMENT,USB_PROBE_IO,USB_PROBE_LAYOUT };
enum usb_probe_result xhci_observe(const struct xhci_observer *io,struct xhci_snapshot *out);
const char *usb_probe_error(enum usb_probe_result result);
struct pi5_usb_description {
    char path[256],status[16];
    uint64_t bus_address;
    uint32_t span;
};
/* Recognizes only the two documented RP1 DWC3 nodes, not arbitrary DT MMIO. */
bool pi5_usb_describe(const void *dtb,size_t available,unsigned index,struct pi5_usb_description *out);
void usb_setup(const char *board,uintptr_t dtb,uintptr_t uart_base);
bool usb_command(const char *line);
#endif
