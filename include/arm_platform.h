#ifndef OS_ARM_PLATFORM_H
#define OS_ARM_PLATFORM_H

#define ARM_AFFINITY_MASK 0xff00ffffff

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
static inline uint64_t arm_cpu_affinity(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr&ARM_AFFINITY_MASK;
}

enum psci_conduit { PSCI_NONE, PSCI_HVC, PSCI_SMC };
#define PSCI_VERSION 0x84000000u
#define PSCI_CPU_OFF 0x84000002u
#define PSCI_CPU_ON 0xc4000003u
#define PSCI_AFFINITY_INFO 0xc4000004u
#define PSCI_FEATURES 0x8400000au
enum psci_conduit psci_discover(const void *,size_t);
int32_t psci_call_args(enum psci_conduit,uint32_t function,uint64_t arg0,uint64_t arg1,uint64_t arg2);
static inline int32_t psci_call(enum psci_conduit method,uint32_t function,uint64_t arg0)
{
    return psci_call_args(method,function,arg0,0,0);
}
bool arm_platform_command(const char *,const void *dtb);
#endif
#endif
