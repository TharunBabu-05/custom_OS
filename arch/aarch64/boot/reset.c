#include "console.h"
#include "cpu.h"
#include "reboot.h"

enum { PM_RSTC=0x1c, PM_RSTS=0x20, PM_WDOG=0x24 };
#define PM_PASSWORD 0x5a000000u
#define PSCI_SYSTEM_RESET 0x84000009u

static void watchdog_reset(uintptr_t base)
{
    volatile uint32_t *pm=(volatile uint32_t *)base;
    /* Select the default firmware partition and preserve reset cause bits.
     * Arm a ten-tick watchdog and select full reset at expiry. */
    pm[PM_RSTS/4]=PM_PASSWORD|(pm[PM_RSTS/4]&0x00fffaaau);
    pm[PM_WDOG/4]=PM_PASSWORD|10u;
    __asm__ volatile("dsb sy" ::: "memory");
    pm[PM_RSTC/4]=PM_PASSWORD|(pm[PM_RSTC/4]&0x00ffffcfu)|0x20u;
    __asm__ volatile("dsb sy" ::: "memory");
    uint64_t start=timer_ticks(),hz=timer_frequency();
    while (timer_ticks()-start<hz) { __asm__ volatile("yield"); }
    console_write("Reboot: watchdog reset did not complete. Power-cycle the board.\n");
    cpu_halt();
}
void reset_request(const struct reset_target *target)
{
    __asm__ volatile("dsb sy" ::: "memory");
    if (target->method==RESET_BCM2712) { watchdog_reset(target->registers);return; }
    register uint64_t x0 __asm__("x0")=PSCI_SYSTEM_RESET;
    if (target->method==RESET_PSCI_HVC) {
        __asm__ volatile("hvc #0" : "+r"(x0) :: "x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
    } else {
        __asm__ volatile("smc #0" : "+r"(x0) :: "x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
    }
}
