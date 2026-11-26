#include "block.h"
#include "console.h"
#include "irq.h"
#include "reboot.h"
#include "smp.h"
#include "vfs.h"

void system_power_transition(const char *board,uintptr_t dtb,bool poweroff)
{
    const char *name=poweroff ? "Poweroff" : "Reboot";
    struct reset_target target;
    if (!reset_discover(board,(const void *)dtb,0x200000,&target)) {
        console_write(name);
        console_write(" refused: no supported reset method in the device tree.\n");
        return;
    }
    /* The monitor owns the foreground. Filesystem and block operations finish
     * synchronously, and neither the timer nor the probe starts storage I/O. */
    if (vfs_data_mounted()) {
        enum fs_result sync=vfs_data_sync();
        if (sync!=FS_OK) {
            console_write(name); console_write(" refused: /data flush failed: ");
            console_write(ramfs_error(sync));console_write("\n");
            return;
        }
    }
    enum block_result stopped=platform_storage_quiesce();
    if (stopped!=BLOCK_OK) {
        console_write(name); console_write(" refused: storage quiesce failed: ");
        console_write(block_error(stopped));console_write("\n");
        return;
    }
    (void)smp_shutdown();
    console_write(name); console_write(": storage synchronized and idle. ");
    console_write(poweroff ? "Halt via " : "Reset via ");
    console_write(target.method==RESET_BCM2712 ? "BCM2712 watchdog.\n" :
                  target.method==RESET_PSCI_HVC ? "PSCI HVC.\n" : "PSCI SMC.\n");
    uart_drain();
    uint64_t flags=irq_save();
    reset_request(&target,poweroff);
    irq_restore(flags);
    console_write(name); console_write(": reset request returned. Storage is quiesced.\n");
}
