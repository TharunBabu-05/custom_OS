# USB bring-up: discovery and command DMA

The user's Pi 5 boots OurOS from a **32 GB USB pendrive**, with no microSD card
installed. That boot method is valid. Pi firmware reads the FAT partition and
loads the kernel and DTB into RAM before OurOS starts. OurOS then uses its own
UART, timer, page allocator and RAM filesystem. Firmware loading the kernel does
not supply a kernel USB block driver.

The discovery milestone is now observed on the user's Pi: both xHCI windows
respond, and usb0 port 1 is connected/enabled. See the
[capture](../proofs/pi5-usb-discovery-2026-11-08.txt). This revision adds a manual
command/event DMA test, verified against QEMU's real xHCI emulator.
**Pendrive file reads, device enumeration and persistent writes are
not implemented.** The existing `storageinit` still targets microSD on the Pi;
it cannot mount a USB pendrive. `/boot` stays empty after that attachment fails.

## Test using the existing pendrive

Build the header-console image:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Flash `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img` to the pendrive
using the same method as the earlier successful boots. The `-sd.img` suffix is
a retained filename; the image is a partitioned disk image, not restricted to
SD media. Flashing replaces the target drive's contents. No reformatting or SD
card purchase is needed to run this milestone. Keep the same Pi USB port,
CP2102 wiring and 115200 8N1/no-flow-control terminal settings.

At the monitor, run these commands individually:

```text
info
usbinfo
usbprobe
uptime
ls /
```

Capture the entire output of both USB commands. `usbinfo` reads only the DTB
already in RAM. It should list aliases `usb0` and `usb1`, their paths, DT status,
RP1 bus addresses and candidate CPU windows. It performs no MMIO.

`usbprobe` is manual. On the header build it reads each enabled host's xHCI
capabilities, command/status, page-size mask and root-port status. It reports
port connection, enable/power, speed ID, link state and the Supported Protocol
revision when advertised. `speed_id` is the controller's raw ID, not a bit rate.
For example, protocol revision `0x0300` denotes USB 3.0. A connected root port
does not identify a flash drive, its capacity, files or which device booted the
Pi. Root-port numbers also do not directly label the four external sockets.
Firmware may have halted/changed a controller before entering the kernel, so
disconnected/disabled status is evidence to inspect, not an OS boot failure.

This probe does not write USB registers, reset/power ports, clear change bits,
take ownership, allocate DMA rings or issue USB/SCSI commands. It preserves the
RP1 PCIe configuration supporting the serial console. It assumes the captured
firmware RP1 window remains accessible; a bus fault would reach the existing
fatal exception handler. Capture that output and reboot if it happens.

On the dedicated-UART build the RP1 mapping has no live header-UART evidence,
so register probing is disabled. QEMU virt reports that no Pi RP1 host exists
and performs no Pi MMIO. The newer DMA test still needs a physical Pi log.

## New command: usbtest

After recording `usbprobe`, run `usbtest` once. This takes control of **usb0
only**, stops and resets that xHCI controller, installs OurOS-owned DMA buffers,
starts it, submits four No-op commands, checks their completion codes and exact
command pointers, then halts the controller. It changes firmware USB device
state; **reboot before trying it again or expecting firmware's USB setup**.
The serial console and RAMFS are independent of that USB controller. The test
does not reset the RP1 PCIe link, reconfigure the UART, reset individual ports,
enumerate devices or issue USB transfer/storage commands. usb1 is left alone.

The test first validates the known header mapping, MMU/data-cache-off state,
RP1 DMA description, live BCM2712 PCIe BAR2/UBUS inbound mapping and little-endian
mode. It refuses unsupported mappings before changing xHCI. It does not program
PCIe DMA windows or assume CPU physical pointers are device addresses. All rings,
DCBAA and up to 16 scratchpad pages are aligned static kernel memory protected by
the existing kernel reservation. Buffers stay reserved on failure, even if the
controller cannot halt. No firmware DMA pointer is dereferenced. Legacy BIOS
ownership is rejected rather than bypassed.

Expected success:

```text
PASS: four xHCI No-op completions matched; controller halted.
phase=7 completed=4 ... controller_changed=yes halted=yes
```

Capture all output, including BAR2/UBUS values, on refusal or timeout. A successful
No-op tests controller DMA, not USB transfers or pendrive reads. This is one shot
per boot; initialization failures also forbid reuse once a controller write was
attempted. All controller waits have timer deadlines and loop bounds. The event
test intentionally stops before ring wrap; general command queue lifecycle,
device/transfer contexts, recovery and enumeration are later milestones.

## Addressing and bounds

The bounded DT lookup recognizes only Raspberry Pi 5 Model B and its `usb0`/
`usb1` aliases with `snps,dwc3`, host mode, two address/size cells and exact
documented register regions: RP1 `0xc040200000` / `0xc040300000`, each 1 MiB.
It does not use an arbitrary property value as an MMIO pointer.

The CPU candidates are derived from the working header UART at `0x1c00030000`:
subtract the UART RP1 offset `0x30000`, then add USB offset `0x200000` or
`0x300000`. This yields `0x1c00200000` / `0x1c00300000`. Those candidates inherit
the existing firmware mapping assumption; DT/Linux PCI resource addresses alone
do not establish the current bootloader mapping. No PCIe/BAR programming occurs.

The original observer uses a read-only callback, validates capability length,
version, port count and register offsets, caps snapshots at 32 ports, and bounds
the forward extended-capability chain to 64 entries. It never follows DMA
pointers from firmware. Unsupported layouts return an error without ownership
changes. The callback can report an I/O failure in host tests; bare-metal MMIO
bus faults are handled by the kernel's fatal vectors, not recovered locally.

## Next USB milestones

1. Controller discovery: observed on physical Pi in the supplied capture.
2. Host takeover, reset/start, mapped command/event rings and No-op completions:
   native and real QEMU xHCI tests pass; Pi usbtest capture is pending.
3. Enumerate a directly connected device, configure endpoint zero, read USB
   descriptors, and select a configuration. Add hub handling separately.
4. Support USB mass-storage Bulk-Only Transport and bounded SCSI INQUIRY,
   TEST UNIT READY, READ CAPACITY and READ commands. UAS-only devices need a
   separate transport; the driver must check descriptors rather than assume.
5. Expose a read-only block device and reuse the existing FAT32/VFS reader for
   `/boot`. Only successful reads of files such as CONFIG.TXT establish that
   kernel USB storage works. Persistent writes are a subsequent milestone.

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Handoff
```

Native tests exercise actual pinned Pi DT USB aliases/registers, malformed and
truncated descriptions, 2000 DT mutations, mocked xHCI capabilities/ports/
protocols, read failures, bounds/chain rejection and 10000 register mutations.
The callback has no write operation, and tests check that mock registers remain
unchanged. QEMU monitor tests verify both commands refuse Pi MMIO on QEMU virt.
The new native tests also exercise the exact halt/reset/ring engine, scratchpad
and DMA mapping, completion pointer/code errors, timeouts/cleanup, BIOS ownership
and one-shot refusal. Real `qemu-xhci` runs from EL1 and EL2 receive four matching
No-op completions and return to a working UART monitor. The QEMU-only PCI bootstrap
configures only the known qemu-xhci device; it is not a Pi PCIe driver. These
checks do not emulate RP1 hardware or prove USB disk reads. The controller model
also exposed and fixed valid extended capabilities located before CAPLENGTH.

References: [RP1 peripheral manual](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf),
[RP1 USB nodes](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/rp1.dtsi),
[Pi 5 RP1 PCIe/DMA ranges](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts),
[xHCI register definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/usb/host/xhci.h).
