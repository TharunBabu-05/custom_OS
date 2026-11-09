# USB bring-up: read-only pendrive storage

The user's Pi 5 boots OurOS from a **32 GB USB pendrive**, with no microSD card
installed. That boot method is valid. Pi firmware reads the FAT partition and
loads the kernel and DTB into RAM before OurOS starts. OurOS then uses its own
UART, timer, page allocator and RAM filesystem. Firmware loading the kernel does
not supply a kernel USB block driver.

The latest [Pi capture](../proofs/pi5-usb-fat-and-ring-wrap-2026-11-09.txt)
confirms command/event DMA and USB2 enumeration on usb1: VID `0781`, PID `5567`,
configuration 1, interface 0, bulk IN `81`, bulk OUT `02`, 512-byte packets.
The new `storageinit` activates those endpoints, issues read-only SCSI commands
and attempts the existing FAT32 mount at `/boot`. This storage path passes real
QEMU xHCI tests from EL1 and EL2 and is **now observed on the physical Pi**:
SanDisk Cruzer Blade, 30,765,219,840 bytes, FAT32 file listing/CONFIG.TXT reads,
512-sector FNV-1a32 `0x6a004021`, IN/OUT/event wraps `4/2/6`, and successful file
reads, Wi-Fi identification and page test afterward. Persistent writes are not
implemented. The reported disk capacity is larger than the 128 MiB boot
partitioned image; flashing did not expand the FAT volume to fill the drive.

## Current storage test: run this first after reboot

On a fresh boot of the new header image, run these commands individually:

```text
storageinit
storageinfo
ls /boot
cat /boot/CONFIG.TXT
cat /boot/SOURCE.TXT
storagetest
storageinfo
cat /boot/CONFIG.TXT
wifiinit
pagetest
uptime
```

Do **not** run `usbtest` or `usbenum` before `storageinit`: all three share a
one-shot controller session. Reboot if either diagnostic already took over the
host. `storageinit` includes the four No-ops and enumeration itself; a successful
storage session keeps the controller running. Repeating `storageinit` reuses
the existing block device/mount. It does not reset an active storage session.

Success requires `USB BOT: ready, read-only`, a capacity, `/boot: FAT32 read-only`,
real file names and `kernel=OUROS.IMG` in CONFIG.TXT. `storagetest` reads sectors
0..511, prints an FNV-1a checksum, and exercises ring cycling. The following
`storageinfo` should show positive `ring_wraps_in`, `out` and `events` counts.
The checksum depends on the flashed image. It is a transport check, not a
cryptographic image signature. A subsequent `cat` verifies reads remain usable.
Capture all output on failure, including `storageinfo` and `usbprobe`.

The driver supports one directly connected USB2 BOT/SCSI disk, LUN 0, 512-byte
logical sectors and READ CAPACITY(10) capacity below 2 TiB. No hub, USB3, UAS,
hotplug, concurrent I/O or stall/reset recovery is provided. Use the same
pendrive and USB port that passed enumeration. Unsupported/error states stop
attachment; a failed active transport is disabled and a controller halt is
attempted. DMA buffers remain reserved for the whole boot even if halt fails.

The exact disk command allowlist is INQUIRY, TEST UNIT READY, REQUEST SENSE,
READ CAPACITY(10) and READ(10). CBW/CSW signatures, lengths, tags, residue and
status are checked. USB OUT transfers carry command wrappers only, not file
data or write/erase commands. The FAT mount is read-only; `/` remains volatile
RAMFS. This supplies a future source for Wi-Fi firmware files, but does not yet
load or start Wi-Fi firmware.

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
and performs no Pi MMIO. Pi DMA, descriptor reads, BOT sectors and FAT32 file
reads now have physical evidence on the tested header-console setup.

## New command: usbtest

After recording `usbprobe`, run `usbtest` once. It selects the first enabled
controller reporting a connected root port, with usb0 as fallback when none is
connected. It prints the selected host, takes control of **only that controller**,
stops and resets it, installs OurOS-owned DMA buffers,
starts it, submits four No-op commands, checks their completion codes and exact
command pointers, then halts the controller. It changes firmware USB device
state; **reboot before trying it again or expecting firmware's USB setup**.
The serial console and RAMFS are independent of that USB controller. The test
does not reset the RP1 PCIe link, reconfigure the UART, reset individual ports,
enumerate devices or issue USB transfer/storage commands. The other host is left alone.

The test first validates the known header mapping, MMU/data-cache-off state,
RP1 DMA description, live BCM2712 PCIe BAR2/UBUS inbound mapping and little-endian
mode. It refuses unsupported mappings before changing xHCI. It does not program
PCIe DMA windows or assume CPU physical pointers are device addresses. All rings,
DCBAA and up to 16 scratchpad pages are aligned static kernel memory protected by
the existing kernel reservation. Buffers stay reserved on failure, even if the
controller cannot halt. No firmware DMA pointer is dereferenced. Legacy BIOS
ownership is rejected rather than bypassed.

The [2026-11-09 capture](../proofs/pi5-cold-wifi-and-dma-preflight-2026-11-09.txt)
shows firmware BAR2=0x15 and UBUS_REMAP=1: a 64 GiB inbound identity window at
PCI address zero. The previous candidate incorrectly required the relocated
0x1000000000 layout described for Linux, so it refused before xHCI writes.
The decoder now supports this observed identity profile, as well as the bounded
relocated profile. It uses live registers for the address calculation; RP1's
documented direct outbound aperture covers these addresses. Native tests cover
the exact captured values and bounds. This supports the mapping candidate;
only a successful live No-op completion will verify Pi DMA operation.

That subsequent [Pi capture](../proofs/pi5-wlan-selected-usb-dma-pass-2026-11-09.txt)
now shows success: usb1, four matched completions, two scratchpad pages,
command DMA 0x195000, event DMA 0x196000, completion code 1 and a halted
controller. The monitor continues with uptime=90. This verifies the No-op path
on that setup, not USB device or storage transfers.

The new capture also moves the connected root port to usb1. Host selection now
follows a connected controller instead of always choosing usb0. This is a root
port observation, not identification of the boot pendrive.

## New command: usbenum

On a fresh boot run `usbenum` **instead of usbtest**. Both share the one-shot
controller workspace and require a reboot before reuse. Enumeration starts
with the same No-op test, then resets one connected USB2 root port, enables a
slot, supplies an input/device context and endpoint-zero transfer ring, and
issues Address Device. It reads the first eight device-descriptor bytes,
updates the endpoint packet size if needed, reads the full device descriptor,
then the first configuration header and its bounded full descriptor chain.

Output includes actual VID/PID, USB version, device class, root port, slot and
EP0 packet size. A class 08/subclass 06/protocol 50 interface with two bulk
endpoints is reported as a Bulk-Only Transport storage interface, including
endpoint addresses and packet sizes. This identifies a possible storage
transport; **no configuration is activated and no SCSI or disk command runs**.
The controller is halted at the end. RAMFS and UART should remain usable.

Scope is one directly connected low/full/high-speed USB2 device, one configuration
up to 256 bytes, and descriptor inspection. Hubs are not traversed; SuperSpeed,
UAS transport, general endpoint/stream queues, hotplug, ring wrap and recovery
are not implemented. Use the same USB2 connection shown in the hardware log.
All context/TRB/data buffers remain reserved kernel memory, even on failure.
Request waits and descriptor traversals are bounded, and completion type,
pointer, code, endpoint and slot are checked. Endpoint-zero setup/data/status
TRBs use a single outstanding transfer; no interrupt driver is required.

The QEMU test now has `-Enumerate`, attaching an existing image read-only to
USB storage with USB3 ports disabled so the fixture uses USB2. EL1 and EL2
tests read device/configuration descriptors, identify BOT endpoints, reject
repeat use, verify the monitor/RAMFS/page allocator still work, and preserve
the backing image hash. This does not prove the new descriptor path on RP1.

Expected success:

```text
PASS: four xHCI No-op completions matched; controller halted.
phase=7 completed=4 ... controller_changed=yes halted=yes
```

Capture all output, including BAR2/UBUS values, on refusal or timeout. A successful
No-op tests controller DMA, not USB transfers or pendrive reads. This is one shot
per boot; initialization failures also forbid reuse once a controller write was
attempted. All controller waits have timer deadlines and loop bounds. The event
test intentionally stops before ring wrap. The storage path below adds bulk
and event ring cycling; general command queue lifecycle and recovery remain
later milestones.

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
   native/QEMU checks pass and the Pi usbtest capture now confirms this path.
3. Direct USB2 device/configuration descriptors and BOT endpoints: now observed
   on the Pi, including VID/PID, speed, endpoint addresses and packet sizes.
4. Configuration selection, bulk contexts/rings, BOT, read-only SCSI and FAT32
   `/boot` pass QEMU tests and the physical Pi capture, including reads after wrap.
5. Current radio work: discover chip cores/RAM, then use storage to supply Wi-Fi firmware.
   Hub/UAS/USB3 support, lifecycle recovery and persistent writes remain separate
   milestones. Only actual file reads establish a usable kernel storage path.

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Handoff
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Enumerate
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Enumerate -Handoff
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Storage
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-usb-qemu.ps1 -Storage -Handoff
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
checks do not emulate RP1 hardware. Storage runs additionally mount FAT32,
read real files, compare a 512-sector checksum against the host, cross both bulk
and event ring boundaries, repeat the mount, and check the backing SHA256 is
unchanged. Native BOT tests cover capacity/sense retries and malformed/short
CSW, tag/residue/status errors, bounds and failure reuse rejection. The controller model
also exposed and fixed valid extended capabilities located before CAPLENGTH.

References: [RP1 peripheral manual](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf),
[RP1 USB nodes](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/rp1.dtsi),
[Pi 5 RP1 PCIe/DMA ranges](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts),
[xHCI register definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/usb/host/xhci.h),
[BOT framing and status handling](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/usb/storage/transport.c).
