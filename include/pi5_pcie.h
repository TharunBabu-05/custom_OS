#ifndef OS_PI5_PCIE_H
#define OS_PI5_PCIE_H
#include "dt_resources.h"

enum pi5_pcie_port { PI5_PCIE_EXTERNAL, PI5_PCIE_RP1 };
enum { PI5_PCIE_MAX_WINDOWS=16 };

struct pi5_pcie_bridge {
    struct dt_node node;
    uint64_t registers;
    uint32_t domain,lanes,generation;
    enum dt_state state;
};

struct pi5_pcie_window {
    uint64_t pci,cpu,bytes;
    bool wide,prefetch;
};

bool pi5_pcie_describe(const void *,size_t,enum pi5_pcie_port,struct pi5_pcie_bridge *);
/* Return the verified count. A zero result means no valid window table. */
unsigned pi5_pcie_windows(const void *,size_t,const struct pi5_pcie_bridge *,bool inbound,
                          struct pi5_pcie_window out[PI5_PCIE_MAX_WINDOWS]);
void pi5_pcie_command(const void *dtb,enum pi5_pcie_port,bool windows);
#endif
