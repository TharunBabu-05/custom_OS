# Phase 3 safe reboot

Run `reboot` at the kernel monitor prompt `>`. From the EL0 shell, type
`exit` first. A reboot discards RAMFS and session state. Files committed to
`/data` are available after the next mount.

The command validates the reset mechanism against the live device tree
before touching storage. It flushes a mounted `/data` using the existing
synchronous cache flush. A failed flush leaves the monitor running and
prevents the reset. Once the foreground I/O has finished, the command
stops the owned xHCI controller and waits for the halted status. QEMU's
read-only virtio backend instead waits for a device reset. The Pi SD
handoff uses synchronous read-only PIO. A storage stop failure also
prevents reset. The UART has time to transmit the final line before reset.
If a PSCI request returns, the monitor reports that storage is quiesced.

The Pi 5 uses the BCM2712 power-management watchdog and the default
firmware boot partition. QEMU `virt` uses PSCI `SYSTEM_RESET` through the
device-tree `hvc` or `smc` method. [Pi watchdog driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/watchdog/bcm2835_wdt.c) · [Pi 5 device tree](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi) · [QEMU virtual board](https://raw.githubusercontent.com/qemu/qemu/v9.2.0/hw/arm/virt.c)

## Verify on the Raspberry Pi 5

Apply the boot update to the existing header-UART installation. With the
automatic shell open, hash the fixture already saved during Phase 0, then
exit to the monitor, check both banks, and reboot.
[Windows update and rollback](boot-updates-tharun_verified.md)

```text
sha256 /data/phase0/large.bin
exit
datacheck
reboot
```

The monitor should print `Reboot: storage synchronized and idle. Reset via
BCM2712 watchdog.` Wait for the fresh kernel banner and automatic shell.
Then hash the file again and check the mounted generation.

```text
sha256 /data/phase0/large.bin
exit
datacheck
timertest
```

The hashes should be identical, and `datacheck` should still match the
mounted generation from before the reset. If using a clean `/data`, first
copy `/boot/LARGE.BIN` to `/data/phase0/large.bin` after creating `/data/phase0`.
The fixture SHA-256 is
`a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3`.

## Host verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-reboot.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -Reboot
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PeripheralIrq
```

The native test checks the pinned Pi device tree, both generated QEMU device
trees, and the command sequence. It checks refusal on a busy or failed flush,
a storage halt timeout, and an unsupported target, and verifies that an
unmounted `/data` needs no flush.

Both QEMU profiles produced a fresh boot banner with and without mounted
USB storage. The data test wrote a 16 KiB fixture, observed the same
SHA-256 and snapshot generation after reboot, and verified that the boot
region stayed byte-for-byte identical. The RAMFS marker disappeared after
reset and the periodic timer passed again. Both profiles also remounted
read-only virtio storage after reboot with the disk hash unchanged.

## Boot update

The boot-update ZIP was built from `a2358ac` plus the preceding working-tree
phases and Phase 3. The updater passed its preview, installation and rollback
checks. Preview against the verified Phase 2 payload selected `OUROS.IMG`
only. The Pi 5 reboot test is pending.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-boot-update.zip` | `25113927a30cccaa0884761e3c1e83f50b0eec858e19a24fface4fa715e6653b` |
| `OUROS.IMG` | `d60ec052f83150fe3345942113b90b4c9e6ca4dbf7963d1c1174bbaf295cdfc9` |
