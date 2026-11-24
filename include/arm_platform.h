#ifndef OS_ARM_PLATFORM_H
#define OS_ARM_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARM_AFFINITY_MASK 0xff00ffffffULL
enum { ARM_CPU_LIMIT=16 };

struct arm_cpu {
    uint64_t affinity;
    bool enabled,psci;
};

struct arm_topology {
    struct arm_cpu cpu[ARM_CPU_LIMIT];
    unsigned count;
};

bool arm_topology_read(const void *,size_t,struct arm_topology *);
enum psci_conduit { PSCI_NONE, PSCI_HVC, PSCI_SMC };
enum psci_conduit psci_discover(const void *,size_t);
int32_t psci_call(enum psci_conduit,uint32_t function,uint64_t arg0);
bool arm_platform_command(const char *,const void *dtb);
#endif
