# Exceptions, GICv2 and periodic timer interrupts

The [latest Pi capture](../proofs/pi5-mmu-usb-protection-2026-11-12.txt) passes
MMU activation, all five protection faults repeatedly, heap/page tests, RAMFS
edits and read-only USB/FAT sector/file reads. That verifies the previous MMU
phase on the tested board. This revision adds the **EL1 exception/IRQ return
foundation, GICv2 controller setup and periodic physical timer interrupts**.
The new IRQ behavior still needs its own Pi test.

This remains a single-core, non-preemptive kernel monitor. Timer interrupts
update counters and schedule the next hardware deadline; they do not switch
tasks. No EL0 processes, general user-fault recovery, peripheral IRQ drivers,
nested IRQ support or scheduler are implemented by this phase. The complete
[OS/driver checklist](os-status.md) tracks the remaining subsystems.

## Image and Pi test

Extract the complete [disk ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the USB pendrive, or use the local full
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). Previous full images
and ZIPs remain directly in `image_files` under dated names. The scripts write
regular image files, not a physical device; no Git commit or push is performed.

Use the existing CP2102 header UART at 115200 8N1, no flow control and PuTTY
local echo/line editing off. Run one command at a time at the normal `>` prompt,
waiting for its result:

```text
mmuinit
exctest
mmutest
irqinit
irqinfo
timerstart
timertest
timerinfo
heaptest
pagetest
write /tmp/irq.txt timer-interrupts-ok
cat /tmp/irq.txt
rm /tmp/irq.txt
storageinit
cat /boot/CONFIG.TXT
storagetest
timertest
timerinfo
timerstop
timerinfo
exctest
mmutest
uptime
```

Expect these markers:

- `exctest`: PASS with all 31 general registers, SP and NZCV preserved through
  a deliberately armed synchronous exception.
- `irqinit`: `GICv2 initialization: ok timer_INTID=30`. CPU IRQs remain masked
  at this point.
- `timerstart`: `Periodic physical timer started: requested_hz=100`.
- `timertest`: PASS for delivered IRQs, asynchronous register/SP/NZCV sentinels
  and responsive foreground execution. It uses bounded busy waits, not `WFI`.
- `timerinfo`: `Timer: running CPU_IRQ=unmasked`, increasing `delivered` counts
  and `fault=no`. Before initialization, empty/zero fields are expected.
- `timerstop`: `Periodic timer stopped; CPU IRQ masked`. Subsequent snapshots
  should show a stopped timer and no continued increase in delivered ticks.

The `probe=... caught` lines in `mmutest` are its five expected protection
faults. The `exctest` trap is also deliberate and returns to the monitor.
Do **not** run the ordinary `fault` command in this Pi sequence: that separate
BRK remains fatal and halts the kernel.

After stopping, try `timerstart`, `timertest`, `timerinfo`, `timerstop` again
to check restart. Each start resets the timer's delivered/elapsed/skipped
counters; controller acknowledgement/completion totals remain cumulative.
`excinfo` shows cumulative exception/IRQ frame counts.

Do not use `usbtest` or `usbenum` before `storageinit` on this boot. The expected
root remains volatile RAMFS, while `/boot` is read-only FAT32. Kernel memory
allocators and file/storage code run in foreground context, not in the ISR.

To regress Wi-Fi, continue after the successful storage test **before
`timerstop`** with the existing [radio sequence](wifi-dns.md#image-and-fresh-boot),
beginning at `wififiles`. Complete firmware/start/control/capabilities, country
IN, scan/target and `wificonnect Jack's`. At `net>` run DNS and hostname/numeric
pings, then `disconnect`. Back at `>`, inspect `timerinfo`, run `timertest`
and stop the timer. Timer IRQs continue while polling drivers or the network
console are active. Credentials are only entered at the hidden prompt and
are not saved or embedded.

Share the output from `mmuinit` through the second `mmutest`/`uptime`, especially
the initialization result, both timer snapshots, `timertest` and storage
checks. The full radio transcript is only needed if that optional regression
fails or differs. If initialization is refused or `fault=yes` appears, capture
`irqinfo`/`timerinfo` and reboot before retrying the failed controller path.

## Controller and timer scope

Boot still masks CPU IRQs. Both MMU and IRQ activation are explicit:
`mmuinit` first, `irqinit` second, then `timerstart`. Initializing the controller
alone never unmasks the CPU. FIQ, SError and debug exceptions remain masked.
`timerstop` disables the physical timer, disables/clears
the owned PPI and masks CPU IRQ delivery.

The bounded DT reader accepts the known Pi 5 or QEMU GICv2/timer profile. Pi
addresses are distributor `0x107fff9000` and CPU interface `0x107fffa000`;
QEMU uses `0x08000000` and `0x08010000`. Their existing MMU device mappings
cover these windows. DT parent phandles, register geometry, binding, status,
timer wiring and CPU0 PPI mask are checked before controller access.

The supported timer is the **non-secure physical architectural timer**,
PPI14 / interrupt ID 30. It uses the physical counter and `CNTP_CVAL_EL0` /
`CNTP_CTL_EL0`. The IRQ interval is `floor(counter_frequency / 100)` ticks;
the actual rate is frequency divided by that interval. The observed Pi
54 MHz and QEMU 62.5 MHz frequencies divide exactly. This is not a hard
real-time delivery or jitter guarantee.

The GIC driver checks its v2 identity, CPU targeting and register readbacks.
On a uniprocessor GIC, zero/RAZ CPU-target registers are accepted because the
banked PPI does not require SPI target routing; multicore profiles require an
accessible one-hot target. Only PPI30's enable, pending, priority and trigger
fields are configured. Global SPI defaults and security groups are not
rewritten. CPU priority masking and GIC interface enable/control necessarily
affect the current CPU interface; they are not a general interrupt policy.

**Firmware handoff assumption:** EL1 is non-secure and firmware exposes the
non-secure physical timer as Group1. The driver uses the non-secure GICv2
interface and leaves secure `IGROUPR` configuration to firmware. Accessible
register readbacks are checked, and `timertest` proves actual delivery; a
successful DT parse alone cannot prove security-group routing on hardware.
An unsupported/restricted firmware setup must be diagnosed rather than
guessed around. This is not a GICv3/ITS/LPI or general platform interrupt driver.

Each timer interrupt masks its level source, calculates a new absolute
deadline, rearms it, and completes the raw GIC acknowledgement token with
EOIR. The driver selects combined priority-drop/deactivation mode, not split
EOI/deactivate operation. Reserved/spurious IDs are counted and not EOIed.
The handler does not allocate, print to UART, access files or call Wi-Fi.

If service is late, deadline arithmetic skips missed intervals in O(1) and
advances to a future deadline instead of issuing a burst of catch-up IRQs.
`delivered` counts handler expirations; `elapsed_periods` includes skipped
periods, and `skipped` counts those missed intervals. The arithmetic checks
overflow and does not support counter wrap. `uptime` continues using the
architectural counter, not the delivered interrupt count.

Dispatch drains at most 16 acknowledgements per entry. Unexpected interrupt
IDs are recorded and masking is attempted; controller errors, unsupported
sources, arithmetic failure or a dispatch storm stop the timer and return
with CPU IRQ masked and a fault flag. The monitor can then report diagnostics.
It does not silently resume a failed IRQ controller; reboot before retrying.

## Exception frames and return

The vector entry creates a 288-byte, 16-byte-aligned frame containing x0..x30,
ELR, SPSR, ESR, FAR and vector index. C dispatch handles the current EL1/SPx
synchronous or IRQ entry, then assembly restores registers and the return
ELR/SPSR before `ERET`. This preserves foreground execution across timer IRQs.
FP/SIMD use remains disabled; this frame is not a future SIMD-task context.

Only the exact armed `exctest` BRK and existing armed MMU probes may resume
from synchronous faults. Other exceptions print their class and saved
diagnostics, stop the timer and halt. There is no general signal delivery,
page-fault repair, EL0 syscall handling, emergency-stack recovery or recovery
from a real stack overflow. Common vector/frame plumbing does not make all
exception classes recoverable.

`exctest` loads public register patterns, triggers its whitelisted BRK and
checks every GPR, the stack pointer and condition flags. `timertest` uses two
asynchronous sentinel passes so each pass's counter/deadline scratch registers
are checked by the other pass, then checks that timer deliveries progressed.
Its clock and iteration limits avoid sleeping forever if IRQ delivery is
broken. These tests establish return behavior for this kernel; they do not
implement task preemption or scheduling.

## Verification

Native tick tests check exact/fractional periods, early delivery, delayed
deadlines against an independent reference, long stalls and overflow/state
preservation. GIC tests check the bounded setup and owned PPI, register
readbacks, acknowledgements/EOI, special IDs and failure paths. The DT tests
exercise the accepted board profiles and reject mismatched bindings/wiring.

QEMU Cortex-A53/A76 tests cover actual timer IRQ delivery, synchronous and
asynchronous register sentinels, stop/restart, heap/PMM/RAMFS/MMU activity and
the continued fatal behavior of ordinary BRK. Firmware-style EL1/EL2 entry and
USB/FAT activity are checked separately. The new timer/GIC-on Pi path still
requires the physical sequence above; emulator success is not hardware proof.

## Packaged image and completed checks

Full disk: **134217728 bytes** (128 MiB). ZIP: **808727 bytes**.
Kernel `OUROS.IMG`: **363964 bytes**. Full-disk SHA256:

```text
fe96709f2c2790bd0410c52e2d83afbe25abf8596a47f8ff3edb906b9561541d
```

Kernel/BSS/guard/stack ends at `0x130000`, below the unchanged DTB window
at `0x200000`. Prior full MMU IMG/ZIP are preserved as
`OurOS-Pi5-header-experimental-sd-20261112-212527-675.img` and
`OurOS-Pi5-header-experimental-sd-20261112-212530-234.zip` in `image_files`.

Completed checks are recorded in:

- `build/phase-irq-final-gic.log`: exact register effects, failure/readback
  paths, Pi/QEMU EL1/QEMU EL2 DT profiles, truncations and mutations.
- `build/phase-irq-tick.log`: exact/fractional periods, independent delayed
  deadline model, missed-period accounting, bounded overflow/state failures.
- `build/phase-irq-final-context.log`: Cortex-A53/A76 actual IRQ delivery,
  synchronous/asynchronous GPR/SP/NZCV preservation, stop/restart, no progress
  after stop, MMU activation deferred without consuming its attempt while
  timer runs, heap/PMM/files and unexpected BRK fatal diagnostics.
- `build/phase-irq-final-handoff.log`: the same checks through EL1 and EL2
  firmware-style entry. QEMU's EL2-specific GICH/GICV windows and extra timer
  tuple are validated but not used by the EL1 physical-timer driver.
- `build/phase-irq-monitor-qemu.log`: comprehensive A53/A76 monitor, crypto,
  memory/filesystem/virtio storage with timer IRQs running; prior MMU disk
  used as read-only media, with its hash unchanged.
- `build/phase-irq-final-usb-qemu.log`: final packaged disk through EL2-to-EL1
  USB BOT/FAT/firmware-file reads, sector hash, all DMA ring wraps and repeated
  mounts, with timer IRQs active and final interrupt tests/stop.
- `build/phase-irq-image.log`: MBR/FAT/config/files and independently extracted
  ZIP/disk hash. `7z t` also passes.
- `build/phase-irq-image-negative.log`: corrupted MBR/FAT/kernel and wrong
  console profile rejected; original image unchanged.

Known generated files/test disks are cleaned after verification. Current
artifacts, dated full-image backups, ELF/maps and diagnostic logs are retained.
No physical device was written and no Git commit or push was performed.
