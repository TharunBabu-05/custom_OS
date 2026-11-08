#ifndef OS_XHCI_TEST_H
#define OS_XHCI_TEST_H
#include "usb.h"
#define XHCI_RING_TRBS 256
#define XHCI_SCRATCHPADS 16
struct xhci_trb { uint32_t parameter_lo,parameter_hi,status,control; };
struct xhci_test_memory {
    _Alignas(4096) struct xhci_trb command[XHCI_RING_TRBS];
    _Alignas(4096) volatile struct xhci_trb event[XHCI_RING_TRBS];
    _Alignas(64) uint64_t dcbaa[65];
    _Alignas(64) uint64_t scratch_array[XHCI_SCRATCHPADS];
    _Alignas(64) struct { uint64_t base; uint32_t size,reserved; } erst;
    _Alignas(4096) unsigned char scratch[XHCI_SCRATCHPADS][4096];
};
struct xhci_test_io {
    struct xhci_observer observer;
    bool (*write32)(void *context,uint32_t offset,uint32_t value);
    bool (*map_dma)(void *context,const void *address,size_t length,uint64_t *bus);
    void (*barrier)(void *context);
    uint64_t (*ticks)(void *context);
    uint64_t hz;
};
enum xhci_test_result { XHCI_TEST_OK,XHCI_TEST_ARGUMENT,XHCI_TEST_LAYOUT,XHCI_TEST_DMA,
    XHCI_TEST_TIMEOUT,XHCI_TEST_IO,XHCI_TEST_EVENT,XHCI_TEST_USED,XHCI_TEST_LEGACY };
enum xhci_test_phase { XHCI_PREFLIGHT,XHCI_HALT,XHCI_RESET,XHCI_SETUP,XHCI_START,XHCI_NOOP,XHCI_STOP,XHCI_DONE };
struct xhci_test_state {
    bool attempted,halted;
    enum xhci_test_phase phase;
    unsigned scratchpads,completed;
    uint32_t last_status,last_event_type,last_completion;
    uint64_t command_dma,event_dma,last_pointer;
};
/* One shot. Caller reserves memory for the entire boot, including failures. */
enum xhci_test_result xhci_ring_test(const struct xhci_test_io *io,struct xhci_test_memory *memory,struct xhci_test_state *state);
const char *xhci_test_error(enum xhci_test_result result);
void xhci_test_report(enum xhci_test_result result,const struct xhci_test_state *state);
void platform_usb_test(uintptr_t dtb,uintptr_t uart_base);
/* Live BCM2712 BAR2/remap decoder; does not write PCIe registers. */
struct pi5_dma_window { uint64_t cpu,bus,size; uint32_t bar_lo,bar_hi,remap_lo,remap_hi; };
bool pi5_dma_window_read(const struct xhci_observer *bridge,struct pi5_dma_window *out);
bool pi5_dma_address(const struct pi5_dma_window *window,uint64_t physical,size_t length,uint64_t *bus);
bool pi5_dma_description(const void *dtb,size_t available);
#endif
