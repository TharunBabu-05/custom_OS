#include "console.h"
#include "cpu.h"
#include "reboot.h"
#include "arm_platform.h"

enum { PM_RSTC=0x1c, PM_RSTS=0x20, PM_WDOG=0x24 };
#define PM_PASSWORD 0x5a000000u
#define PSCI_SYSTEM_RESET 0x84000009u
#define PSCI_SYSTEM_OFF 0x84000008u

static void watchdog_reset(uintptr_t base,bool poweroff)
{
    volatile uint32_t *pm=(volatile uint32_t *)base;
    /* Encode the firmware halt partition (63) or the default partition (0).
     * The partition bits occupy alternate positions in RSTS. */
    pm[PM_RSTS/4]=PM_PASSWORD|(pm[PM_RSTS/4]&0x00fffaaau)|(poweroff ? 0x555u : 0);
    pm[PM_WDOG/4]=PM_PASSWORD|10u;
    __asm__ volatile("dsb sy" ::: "memory");
    pm[PM_RSTC/4]=PM_PASSWORD|(pm[PM_RSTC/4]&0x00ffffcfu)|0x20u;
    __asm__ volatile("dsb sy" ::: "memory");
    uint64_t start=timer_ticks(),hz=timer_frequency();
    while (timer_ticks()-start<hz) { __asm__ volatile("yield"); }
    console_write("Watchdog reset did not complete. Power-cycle the board.\n");
    cpu_halt();
}
int32_t psci_call_args(enum psci_conduit method,uint32_t function,uint64_t arg0,uint64_t arg1,uint64_t arg2)
{
    register uint64_t x0 __asm__("x0")=function;
    register uint64_t x1 __asm__("x1")=arg0;
    register uint64_t x2 __asm__("x2")=arg1;
    register uint64_t x3 __asm__("x3")=arg2;
    if (method==PSCI_HVC) {
        __asm__ volatile("hvc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) ::
                         "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    } else if (method==PSCI_SMC) {
        __asm__ volatile("smc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) ::
                         "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    } else {
        return -1;
    }
    return (int32_t)x0;
}

void reset_request(const struct reset_target *target,bool poweroff)
{
    __asm__ volatile("dsb sy" ::: "memory");
    if (target->method==RESET_BCM2712) { watchdog_reset(target->registers,poweroff);return; }
    enum psci_conduit method=target->method==RESET_PSCI_HVC ? PSCI_HVC : PSCI_SMC;
    (void)psci_call(method,poweroff ? PSCI_SYSTEM_OFF : PSCI_SYSTEM_RESET,0);
}
