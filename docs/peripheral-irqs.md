# Phase 2 peripheral interrupts

The GICv2 dispatcher can route up to four registered shared peripheral
interrupts (SPIs). A device callback clears its level in IRQ context before
the dispatcher writes the original acknowledge token to EOIR. Each source
can queue eight callbacks for the kernel monitor to service in round-robin
order outside IRQ context. The IRQ path uses fixed storage and bounded work.
The existing 100 Hz physical timer on PPI30 still schedules EL0.

The dispatcher masks a registered line if the device fails to clear it or
its deferred queue fills. `irqinfo` reports `peripheral_received`, `deferred`,
`overflow` and `stuck`. An unexpected maskable line is disabled and counted
separately. An unmaskable source or controller error stops IRQ delivery.

The monitor command `irqtest` temporarily claims an idle line and raises a
one-shot event. The Pi 5 uses BCM system timer compare channel 3, INTID 99.
QEMU `virt` uses the PL031 alarm, INTID 34. In both cases OurOS validates the
device-tree route before accessing registers. The command restores the saved
compare register, priority and CPU target when it releases the line.
[GICv2 programming reference](https://arm-software.github.io/CMSIS_5/Core_A/html/structGICDistributor__Type.html) · [BCM system timer driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/clocksource/bcm2835_timer.c) · [QEMU virt board](https://qemu.googlesource.com/qemu/+/25fef09ce17ac1ae22638a0b57d97c2bd5cd7d83/hw/arm/virt.c)

## Verify on the Raspberry Pi 5

Apply the boot package to the existing header-UART installation with the
Windows updater. From a fresh serial shell, run these commands one at a time.
[Windows update and rollback](boot-updates-tharun_verified.md)

```text
exit
irqinfo
irqtest
irqtest
irqinfo
timertest
cat /boot/CONFIG.TXT
datacheck
appshell
run /boot/BUSY.ELF
run /boot/DF.ELF
exit
```

Both probes should report `BCM system timer compare 3 intid=99 received=1
deferred=1`, a positive `timer_delta` and the peripheral PASS line. After two
probes, `irqinfo` should show `peripheral_received=2 deferred=2 overflow=0
stuck=0`, a running timer and `fault=no`. The remaining commands exercise
timer context preservation, FAT reads, mounted snapshot verification, EL0
preemption and shell cleanup. `datacheck` should match the mounted generation.

The Pi 5 passed on 2026-11-22. Both runs delivered INTID 99 and each deferred
callback completed while the architectural timer advanced five ticks. The
timer context check passed, the EL0 shell recorded four preemptions and both
children exited with code 0. `/boot/CONFIG.TXT` was readable and `/data`
generation 14 matched its valid bank. COM3 was initially silent. The
successful capture begins at the running shell after the power cycle.
[Pi serial capture](../proofs/pi5-peripheral-irqs-2026-11-22.txt)

## Host checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-gic.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PeripheralIrq
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-irq-qemu.ps1 -Handoff
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -MemoryPressure
```

The native model covers ownership and adjacent register preservation,
claim rollback, raw EOIR, unexpected source masking, stuck sources and
queue overflow. On Cortex-A53/EL1 and Cortex-A76/EL2, QEMU delivered one
PL031 alarm and one deferred callback on each of two consecutive runs.
The timer stayed active and preempted EL0. UART commands, FAT reads and
snapshot verification passed, and each disk retained its original SHA-256.
The timer stop/restart and exception regression passed with virtualization
off and on. Phase 1 memory pressure also passed on both QEMU profiles.

## Boot update

The package includes Phase 2 on top of commit `a2358ac` and the preceding
working-tree phases. `scripts/package-rpi5-update.ps1` built and verified
`build/rpi5-header/OurOS-Pi5-header-boot-update.zip`. The updater regression
passed preview, apply, repeat and rollback. Previewing against the Phase 1
boot files selected only `OUROS.IMG`.

On 2026-11-22 Windows found the Phase 1 kernel on the SanDisk boot volume.
The updater backed it up and verified the Phase 2 kernel on the drive.
The boot-file inventory showed a change to `OUROS.IMG` only. The rollback
copy is under `backups/pi5-boot-20261122-001636-341cc1b3`.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-boot-update.zip` | `7d5254a6fcee687d5a95706a2b65a955963d1692360a754b0ca6b99f924a6a96` |
| `OUROS.IMG` | `84eda78b939a0bc54fe82745ae27580cb9bd836308615b83b064fb65a52645d9` |
