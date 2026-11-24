# OurOS Pi 5 platform driver plan

The serial console is the development and recovery path. Each driver can
ship in a boot-file update once its test passes. The baseline is the
tested Pi 5 Model B booting the SanDisk USB drive with FAT32 at `/boot`
and extents v5 at `/data`. The pin for the firmware device tree is in
[boot/rpi5/assets.json](../boot/rpi5/assets.json). The [Pi proof index](../proofs/README.md)
records which behaviors have been observed on the board.

## Driver sequence

| Order | Hardware | Starting point and implementation | UART acceptance |
| --- | --- | --- | --- |
| 12 | BCM2712 AVS thermal, firmware mailbox, onboard RTC | Add `platforminfo`, `therminfo`, `powerinfo` and `rtcinfo`. Bind known resources from the firmware DTB and bound mailbox requests. | Read the temperature and power status. Read the RTC twice and check that it advances if initialized. Confirm UTC and the mounted data generation are preserved. |
| 13 | BCM2712 and RP1 buses, clocks, resets, regulators | Inventory enabled, disabled and unresolved DT devices. Resolve address translation and dependencies. Establish who can claim each register range, interrupt and DMA channel. | Match the live inventory to the pinned DTB, identify conflicts and unsupported bindings, and exercise the emulator with malformed DTBs. |
| 14 | GIC, architectural timer, RP1 interrupt controller and DMA | Build on the tested timer PPI and peripheral SPI dispatcher. Validate RP1 interrupt routing, masking and acknowledges. Add DMA address constraints and cache maintenance before a cache-enabled driver uses them. | Repeat interrupts and error recovery while the serial shell, timer and USB reads are active. Verify DMA buffer bounds and cleanup. [IRQ phase](peripheral-irqs.md) |
| 15 | RP1 GPIO, pinctrl, UART, I²C, SPI, PWM and PIO | Reserve console, Wi-Fi and board-owned pins. Add pin claims and GPIO readback, then explicit pinmux and bus controllers. Add PIO pin and state machine ownership after GPIO is verified. | Test GPIO loopback and edge delivery, UART loopback, I²C with a known sensor, SPI with a known device, and PWM or PIO with measured output. |
| 16 | RP1 xHCI, USB hubs, storage and HID | Extend the tested USB2 BOT boot path to enumerate topology and bind interfaces. Add a keyboard with hotplug, then USB3 and broader storage. | Show a keyboard event on UART, unplug and reconnect it, and verify boot-drive reads and persistent file hashes throughout. |
| 17 | SDHCI, microSD, SDIO Wi-Fi and Bluetooth UART | Extend the SD handoff into controlled card detection, read recovery and write/flush on spare media. Keep the tested Wi-Fi interface alive during other driver tests. Discover and initialize Bluetooth HCI separately. | Check a spare SD card across removal and reboot, repeat DHCP and socket echo, and read a Bluetooth HCI controller version. [Storage](storage.md) · [Wi-Fi](background-networking.md) |
| 18 | RP1 Gigabit Ethernet MAC and PHY | Add MDIO, link negotiation, DMA rings and link change handling. Reuse the IP stack through interface operations with explicit route selection. | Connect to a known LAN peer, verify link transitions, DHCP, UDP and TCP echoes, then test route changes with both interfaces present. |
| 19 | External PCIe and NVMe | Discover the optional root port and endpoint, allocate BAR resources and verify DMA mapping. Add NVMe discovery and read-only queues before supporting writes on a spare drive. | Enumerate the installed endpoint, read a known partition and compare hashes across controller recovery and reboot. |
| 20 | RTC setting and alarms, power management, fan and watchdog | Add operator-authorized RTC updates, optional battery-backed retention and alarm test. Build safe poweroff on verified storage flush and network teardown. Confirm firmware ownership and board limits before fan or PMIC control. | Check RTC readback and retention with a battery, one controlled wake event, clean storage after poweroff, and temperature/fan readbacks with a cooling fixture. [Safe reboot](safe-reboot.md) |
| 21 | Cortex-A76 secondary cores and caches | Test per-core stacks, memory barriers, locks, interrupt affinity and TLB invalidation. Stage cache changes with driver-specific DMA checks before a secondary core runs general work. | Compare single-core and multicore runs, verify interprocessor interrupts and allocator invariants, and stress USB/Wi-Fi while checking storage hashes. |
| 22 | Audio, CSI, DSI, HDMI and graphics | Start with USB audio or a supported external codec. Inventory CSI/DSI endpoints and GPU/firmware dependencies. Revisit Phase 11 when the display is available. | Test audio loopback and capture hashes over UART. Use the corresponding camera or screen to verify frame capture and scanout. [HDMI phase](hdmi-framebuffer.md) |

The live inventory should record compatible strings, translated MMIO,
IRQ and DMA resources, clocks, resets, power supplies, pin assignments
and current owner. Firmware-managed, disabled and accessory-dependent
nodes are valid inventory results. Record the EEPROM and USB-C power
negotiation as firmware-owned while the firmware supplies those functions.
Publish an application interface
after the kernel can enforce ownership and recover from errors on that
device. For each update, check the successful path, a failure or detach
path, serial responsiveness, kernel resource cleanup and existing
storage. Run the equivalent native or QEMU checks before the Pi test.

Phase 13 adds a kernel size budget. The kernel build removes unreferenced
sections and reports its linked memory span and remaining space before
the DTB at `0x200000`. This build has 4,096 bytes of space before that
boundary. The linker still enforces it.

## Phase 12 platform diagnostics

- `platforminfo` reports the Pi 5 AVS register, firmware mailbox and
  RTC binding from the live device tree. It performs device-tree reads.
- `therminfo` reads the AVS sample, checks the validity bits and reports
  signed millidegrees Celsius using the Pi 5 DT calibration.
- `powerinfo` asks firmware for the throttling bitmask with a zero
  clear mask. It prints the raw flags and active/sticky undervoltage.
  A firmware rejection appears as `tag refused or unsupported`.
- `rtcinfo` queries the onboard RTC seconds register and shows the UTC
  date when it fits the formatter's range. It leaves the OS wall clock
  and TLS trust policy under the existing `utc` command.

The mailbox client has one low-RAM buffer, used only from the monitor.
It checks the channel and address of each reply before using message
contents. A missing or unexpected completion after submission disables
further mailbox queries for the boot so a late reply cannot match a new
request. The thermal sample remains available through its own register.
The PMIC, fan, charging and RTC alarm controls are Phase 20 work.
[Pi 5 device tree](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)
· [Mailbox property protocol](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)
· [Bare-metal mailbox buffer addressing](https://github.com/rsta2/circle/blob/master/lib/bcmpropertytags.cpp)
· [AVS thermal driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/thermal/broadcom/bcm2711_thermal.c)
· [Firmware throttling driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/hwmon/raspberrypi-hwmon.c)
· [RTC firmware driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/rtc/rtc-rpi.c)

Run the native checks after the pinned firmware asset is installed.
The QEMU handoff suite exercises the unsupported-board path with serial
boot, UTC and disk checks. Use an existing local v5 image, or set
`$image` from the `IMAGE:` line printed by the builder below.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-pi5-health.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-device-inventory.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PlatformHealth -Image $image
```

The Pi returned 41.9 °C and zero throttling flags. The mounted v5
generation remained 9 and UTC remained unset. The original RTC driver
rejected the response. Firmware can report a four-byte result count
while leaving the selector in the first word and writing Unix seconds
to the second word of the eight-byte value buffer. The updated driver
accepts that reply and the full eight-byte reply, and still checks the
mailbox address, tag, buffer size, terminator and selector.
[OurOS Pi capture](../proofs/pi5-platform-health-2026-11-22.txt)
· [Pi firmware reply capture](https://forums.raspberrypi.com/viewtopic.php?p=2388630)
· [Raspberry Pi RTC driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/rtc/rtc-rpi.c#L37-L45)

The native suite passed against the pinned Pi device tree, including
disabled and malformed bindings, bad replies and timeouts. Both QEMU
handoff profiles reached the serial shell and verified that the Pi-only
commands leave UTC and the v5 disk intact. The Pi build keeps the
existing firmware DTB address by compiling the persistent-storage module
for size. The v5 storage regression passed on both QEMU profiles,
including the 32 KiB file copy, reboot readback and fallback after
corruption of the newest generation.

Apply the Phase 12 ZIP to the existing FAT boot volume with the
[boot updater](boot-updates-tharun_verified.md). With `D:\` as the boot
volume, preview the update and then apply it. The scratch copy of the
binary tools release verified one replacement, `OUROS.IMG`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase12-platform-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. From the automatic shell, enter
these commands at the prompt. Wait at least two seconds between the
`rtcinfo` commands.

```text
exit
datacheck
utc
platforminfo
therminfo
rtcinfo
rtcinfo
powerinfo
utc
datacheck
appshell
echo phase12-ok
```

Record the observed temperature, RTC and power results. A successful
RTC sample increments on a second read. Record the actual response
when the RTC has no plausible date or the firmware does not expose
throttling. Compare UTC state and snapshot generations before and after,
and check for `phase12-ok` at the end. A physical capture is required
to accept the hardware readbacks.

## Phase 13 device inventory

`devlist [path|alias]` lists direct children and effective status.
`devinfo <path|alias>` shows compatible strings, register ranges, the
interrupt parent and specifier cells, and supported dependency properties
with their provider paths. It handles common simple-bus and PCI memory
ranges. A missing, malformed or ambiguous translation is reported as
`dt_cpu=unresolved` or as raw `reg` cells for a different address layout.
Firmware and board policy still determine which devices can be driven.
The resource registry below tracks claims by migrated drivers.
[Devicetree specification](https://devicetree-specification.readthedocs.io/en/stable/devicetree-basics.html)

`dt_cpu` comes from the DTB's `ranges`. On the pinned Pi tree, RP1
UART0 translates to `0x1f00030000`. The header console on the tested Pi
uses the live firmware mapping `0x1c00030000`, which `devinfo serial0`
reports separately. Inventory performs no device probes or register writes.

The native suite verified translation and provider lookup against the
pinned Pi DTB and rejected truncated nodes, duplicate phandles and
out-of-window register ranges. Both QEMU handoff profiles verified the
PL011 mapping, a missing node, and boot, UTC and disk integrity after
running the new commands.

Preview and apply the Phase 13 ZIP on the existing boot drive.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase13-inventory-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After the Pi boots into the shell, use the serial monitor. Wait at
least two seconds between the RTC reads.

```text
exit
datacheck
utc
rtcinfo
rtcinfo
devlist /soc@107c000000
devinfo serial0
devinfo usb1
devinfo ethernet0
devinfo /soc@107c000000/rpi_rtc
devinfo /does-not-exist
utc
datacheck
appshell
echo phase13-ok
```

Check that the RTC emits two Unix timestamps, that the console shows
its live address, and that the USB and Ethernet nodes show their RP1
interrupt parent. The missing path should report an error and return
to the prompt. Compare UTC and both data checks, then look for
`phase13-ok`. The Pi confirmed the SoC listing, header UART and RP1
USB/Ethernet paths. RTC seconds advanced from 25 to 33 and the
mounted data generation remained 9.
[Pi device inventory capture](../proofs/pi5-device-inventory-2026-11-22.txt)

## Phase 13 resource ownership

`resinfo` shows current kernel driver claims. Drivers request groups
atomically from a 16-entry foreground registry. MMIO and DMA-backed
RAM share the physical address domain. IRQ numbers and DMA channel
numbers use separate provider domains. Overlap, invalid spans and a
full registry leave all existing claims intact. Claim and release calls
are internal kernel APIs.

The serial console reserves its operational register range. The GIC
claims the distributor, CPU interface and architectural timer PPI.
The xHCI driver claims the selected live controller window and its
DMA backing before takeover. QEMU also claims the PCI function's
configuration window. A Pi preflight failure releases the group before
takeover. A QEMU preflight failure restores its original PCI settings
and releases after readback verifies the restore. Once xHCI takeover
has started, the claims remain until reboot. This includes a controller
that fails to halt. The IRQ probe returns its MMIO and IRQ claims after
the route and deferred handler are restored. A failed restore keeps
the claims reserved.

The runtime integration covers the single-core console, GIC/PPI,
xHCI and IRQ probe paths. Later drivers must register their own
resources. RP1 interrupt routing and DMA channel programming are
Phase 14 work.

The native test covers overlap, domain isolation, capacity and stale
tickets. Both QEMU handoff profiles mounted the USB disk and returned
identical resource reports after two peripheral IRQ probes. The test
verified the v5 image hash before and after the run.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-resources.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PlatformHealth -Image $image
```

Apply the new boot-file update from the project on Windows, using
the current `OUROSBOOT` letter. The scratch update changed only
`OUROS.IMG`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase13-resources-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After booting the Pi, run the following over UART.

```text
exit
datacheck
resinfo
irqtest
resinfo
irqtest
resinfo
datacheck
appshell
echo resources-ok
```

The existing Pi USB boot path should show six claims, including
`serial-console`, `gic-timer` and `usb1-xhci`. The xHCI live base
should be `0x1c00300000`. Both IRQ tests should pass and all three
reports should contain the same claims. Check the mounted data
generation and look for `resources-ok`. The Pi passed three IRQ
probes, reported the same six claims each time and kept generation 9.
[Pi capture](../proofs/pi5-resource-ownership-2026-11-22.txt)

## Phase 14 RP1 interrupts and DMA

`rp1info` resolves the RP1 interrupt parent and PCIe MSI provider from
the boot device tree. After verifying the live PCIe link and outbound
window, it reads the RP1 raw interrupt status and MSI-X configuration
for UART0, USB0, USB1, Ethernet and the DMA engine. It reports the
local enable and acknowledge settings. The bridge's live inbound DMA
window and the xHCI DMA claim's translated address are reported separately.
Raw interrupt bits can change between reads.
[RP1 register guide](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)
· [RP1 Linux MSI-X driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/misc/rp1/rp1_pci.c)
· [BCM2712 PCIe bridge driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/pci/controller/pcie-brcmstb.c)

The Pi and QEMU xHCI mappers now require every DMA range to fit in a
single live claim owned by that driver. They also check the MMU DMA
policy, and Pi checks the live PCIe DMA window. Native tests cover
route decoding, invalid DTs, link and window refusal, and DMA bounds.

The observation command reads only interrupt and window registers.
USB storage polls for its completions.

Use the update ZIP on the existing boot partition.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase14-rp1-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After booting on the Pi, run these in the serial terminal.

```text
exit
datacheck
rp1info
irqtest
storagetest
rp1info
resinfo
datacheck
appshell
echo rp1-ok
```

On the pinned DTB, USB1 is RP1 line 36, Ethernet is line 6 and the
eight-channel DMA engine is line 40. Capture the live settings, the
read-only storage test and the final shell output. Compare the six
claims and the mounted generation with the prior physical capture.
`irqtest` exercises the existing BCM system timer path. The Pi
confirmed both observation runs, all five reported routes, the
64 GiB inbound identity aperture and the 512-sector storage read.
[Pi observation capture](../proofs/pi5-rp1-observation-2026-11-22.txt)

## Phase 14 RP1 DMA engine

`rp1dmatest` copies 64 bytes between two dedicated kernel buffers with
channel 2 of the RP1 DesignWare DMA engine. It checks the firmware DMA
clock, live PCIe windows, address bounds and resource claims before
programming the channel. The driver polls for completion and verifies
the destination, source and guard bytes. It restores the saved
registers and releases its claims after verified cleanup. A timeout
attempts to stop the channel. Unverified cleanup keeps the claims and
buffers reserved for the boot. MSI-X delivery and cache-enabled DMA
are subsequent increments.
[RP1 peripheral datasheet](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)
· [DesignWare DMA reference driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/dma/dw-axi-dmac/dw-axi-dmac-platform.c)
· [RP1 clock reference](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/clk/clk-rp1.c)

The native test exercises descriptor encoding, busy refusal,
completion, errors, timeout and failed cleanup. QEMU checks that the
Pi-only command returns to the monitor and the shell and USB disk remain
usable. Build the native model with
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-rp1.ps1`.
The Pi image still has 4 KiB below the firmware DTB. The build compiles
the VFS, FAT32 reader and kernel file layer for size.

Apply the boot-file update on Windows using the current `OUROSBOOT`
letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase14-dma-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After booting on the Pi, run these in PuTTY.

```text
exit
datacheck
resinfo
rp1dmatest
resinfo
rp1dmatest
resinfo
irqtest
storagetest
datacheck
appshell
echo rp1-dma-ok
```

Record two copy passes, identical resource reports and the unchanged
data generation. Capture any preflight refusal or timeout. If the
monitor reports that cleanup is unverified, the reserved claims should
remain visible in `resinfo`, and the next `rp1dmatest` should refuse.

## Phase 14 DMA retest and MSI receiver

The first DMA copy failed setup/readback twice on the Pi. All six
existing claims were unchanged after each attempt. The IRQ and storage
smoke tests passed and data stayed at generation 9.
[Pi DMA setup capture](../proofs/pi5-rp1-dma-setup-2026-11-22.txt)

The revised DMA test enables the idle engine before programming the
channel, checks required fields separately from optional register
bits, and prints the first failing register offset with its expected
and observed values. Native models cover an engine that gates channel
writes until enabled and a fixed bit in a channel configuration
register. The Pi setup failure did not include a register offset, so
the exact rejected access still requires a hardware retest.
[DesignWare initialization and channel setup](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/dma/dw-axi-dmac/dw-axi-dmac-platform.c)

`rp1info` now shows the BCM2712 MSI receiver's doorbell address and
GIC vector range, plus the live host and VPU masks, edge configuration
and pending host bits. The pinned tree describes 64 vectors starting
at GIC INTID 160. This is the receiver observation step. End-to-end
MSI-X delivery and acknowledgments remain a separate test.
[BCM2712 MSI receiver driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/irqchip/irq-bcm2712-mip.c)

Use the updated boot-file package.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase14-mip-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot the Pi and enter these commands in PuTTY.

```text
exit
datacheck
rp1info
resinfo
rp1dmatest
resinfo
rp1dmatest
resinfo
irqtest
storagetest
datacheck
appshell
echo phase14-retest-ok
```

Capture the two `RP1 MIP` lines and both DMA results. The final
`echo` verifies shell responsiveness. A DMA pass is reported by
`PASS: RP1 DMA copied 64 bytes`.

## Phase 14 DMA signal fix and GIC edge test

The MIP readback passed on the Pi. The receiver reports doorbell
`0xfffffff000` and 64 GIC vectors starting at INTID 160. DMA stopped
at channel 2's signal-enable register, offset `0x390`. Its
`0x07c08004` readback matches the complement of the documented
interrupt bits. The updated test checks and restores only the
documented interrupt fields. The native fixture reproduces the Pi
readback and also checks that a real enable bit that stays set
still blocks the transfer.
[Pi diagnostic capture](../proofs/pi5-rp1-mip-dma-signal-2026-11-22.txt)
· [DesignWare interrupt definitions](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/dma/dw-axi-dmac/dw-axi-dmac.h)

`rp1irqtest` temporarily claims INTID 223. On the known Pi DT layout,
that is MIP vector 63, beyond the RP1 source range. The test configures
the SPI for edge triggering, injects one pending event at the GIC,
and checks that one deferred callback ran while the timer kept ticking.
It restores trigger mode, priority and CPU target and releases its
claim. This verifies the host interrupt path. Endpoint MSI-X delivery
and the RP1 acknowledge path remain separate tests.
[Arm GIC set-pending register](https://support.arm.com/documentation/ddi0601/2022-06/External-Registers/GICD-ISPENDR-n---Interrupt-Set-Pending-Registers)

Apply the package to the existing boot volume, using its current drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase14-edge-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After booting on the Pi, run these in PuTTY.

```text
exit
datacheck
resinfo
rp1dmatest
rp1dmatest
rp1irqtest
rp1irqtest
irqinfo
resinfo
irqtest
storagetest
datacheck
appshell
echo phase14-edge-ok
```

Check for two DMA passes and two `GIC software edge intid=223` lines
with `received=1 deferred=1` and nonzero timer deltas. Compare the
resource reports and the mounted data generation. Capture any
failure line in full. The final shell echo verifies responsiveness.

The Pi passed these checks. It returned two DMA copy passes, two
software-generated edges with one deferred callback each and
`fault=no`. USB storage, six resource claims and v5 generation 9
were preserved. [Pi capture](../proofs/pi5-rp1-dma-edge-2026-11-22.txt)

## Phase 14 RP1 endpoint MSI-X preflight

`rp1msiinfo` examines the RP1 endpoint one function below the on-board
BCM2712 PCIe root port. It checks link and bus numbering, walks the
conventional PCI capabilities, and reports the MSI-X table size,
function enable and mask, and table and pending-bit array addresses.
It translates the BAR addresses through the bridge's live outbound
windows and cross-checks the peripheral BAR against the running UART
address. The command also checks the live inbound windows for the
known 4 KiB MSI doorbell mapping.
[BCM2712 PCIe bridge driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/pci/controller/pcie-brcmstb.c)
· [PCI capability register definitions](https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/pci_regs.h)
· [RP1 C0 MSI-X configuration capture](https://zenn.dev/smallkirby/articles/6bfe4091e92064?locale=en)

The command temporarily selects the endpoint in the bridge's indirect
configuration window and verifies that the previous selector is
restored. Endpoint configuration, MSI-X table contents, and bridge
address windows are read-only in this test. A missing table translation
or doorbell window is an inventory result. Endpoint MSI-X delivery and
acknowledgment remain a later test.

Apply on the existing `OUROSBOOT` partition, using its current letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase14-msix-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After booting on the Pi, run these from PuTTY.

```text
exit
datacheck
resinfo
rp1msiinfo
storagetest
rp1msiinfo
resinfo
datacheck
appshell
echo phase14-msix-ok
```

Capture the doorbell and capability lines from both runs, including
any preflight refusal. Check that both resource reports show six
claims and both mounted generations match.

Both Pi runs reported `inbound=unmapped` and stopped at bridge validation.
USB readback, six active resource claims and `/data` generation 9 were
preserved. The follow-up prints the raw PCIe link status, header type and
primary, secondary and subordinate bus numbers. An active link with a
type-1 bridge header qualifies even when the controller mode bit is
clear. Endpoint access still requires usable firmware bus numbering.
[Pi MSI-X capture](../proofs/pi5-rp1-msix-2026-11-22.txt)

## Phase 15 RP1 header GPIO readback

`gpioinfo <0..27>` is a serial monitor command that uses BCM GPIO
numbers. It reads the pin function, pad pulls, input and output-disable
bits, output-enable override, and the RIO input, output latch and
output enable. RIO state describes the GPIO controller. The function
and pad fields describe how that state reaches the pin. The command
checks the live GPIO device and PCIe window before reading registers.
GPIO 0 and 1 are reserved for HAT identification, and GPIO 14 and 15
are reserved for the header serial console on this boot profile.
[RP1 pin control driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/pinctrl/pinctrl-rp1.c)
· [Raspberry Pi header documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)

Use the existing `OUROSBOOT` partition. Update the boot directory from
PowerShell using its current drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase15-gpio-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject, boot the Pi and run these commands in PuTTY.

```text
exit
datacheck
resinfo
gpioinfo 0
gpioinfo 14
gpioinfo 15
gpioinfo 17
gpioinfo 27
gpioinfo 28
rp1msiinfo
storagetest
resinfo
datacheck
appshell
echo phase15-gpio-ok
```

Expect a GPIO report for each valid pin, the two reservation labels,
and usage text for GPIO 28. Capture the PCIe root status and bus numbers
in the same run. Check that the 512-sector read passes, resource counts
remain at six, and the data generation matches both times. This test
uses the existing serial wiring. Pin ownership and a reversible GPIO
loopback are the next GPIO increment.

The Pi reported GPIO 0, 14, 15 and 17 correctly. GPIO 27 returned the
usage line and needs a retest. The RP1 PCIe link was active with a type-1
header and firmware bus numbers `0/0/0`. The endpoint and MSI-X
delivery remain pending until a PCIe initialization phase can assign
bus numbers and verify the bridge mappings. [Pi GPIO capture](../proofs/pi5-rp1-gpio-2026-11-22.txt)

## Phase 15 pin ownership and isolated RIO test

The Pi header boot profile reserves GPIO 0–1 for `hat-id` and GPIO
14–15 for `serial-console` in the resource table. `resinfo` prints
both GPIO claims and `gpioinfo` gets its reservation labels from that
table. The monitor accepts trailing spaces after the pin number.

`gpiotest` claims one pin, requires disconnected pinmux, a disabled pad
output and a clear RIO output enable, then tests the RIO latch and
enable bits. The pad remains isolated throughout. Restore clears RIO
output enable first, restores the prior latch value, and verifies
both along with the pad and the function before releasing the claim.
GPIO 17 met the isolation prerequisites in the Pi capture. This test
uses the existing serial connection.
[RP1 pad and RIO register reference](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase15-gpio-claims-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive, boot the Pi and run these commands in PuTTY.

```text
exit
datacheck
resinfo
gpioinfo 27
gpioinfo 28
gpiotest 0
gpiotest 14
gpioinfo 17
gpiotest 17
gpiotest 17
gpioinfo 17
storagetest
resinfo
datacheck
appshell
echo gpio-claims-ok
```

Expect a snapshot for GPIO 27, usage for 28, and pin-claim refusals for
GPIO 0 and 14. GPIO 17 should pass twice and report the same pad,
function and latch state. Both resource reports should show eight
claims, including `hat-id` and `serial-console` GPIO, and both data
checks should report the same generation. Physical loopback requires
a separate test with an external connection between two owned pins.

The Pi passed GPIO 27 readback and refused GPIO 28. Both GPIO 17 runs
passed, GPIO 0 and 14 refused the claim, and both inventories showed
eight claims. USB storage passed and `/data` stayed at generation 9.
[Pi ownership capture](../proofs/pi5-rp1-gpio-claims-2026-11-22.txt)

## Phase 15 physical GPIO loopback

`gpioloop` claims GPIO 17 and 27 together. It verifies that both pins
start isolated in the 3.3 V bank, prepares GPIO 27 as an input with a
pull-down, then drives `01010` from GPIO 17. Each level requires four
consecutive matching reads within a bounded sample count. The test
disconnects the output before restoring both pin configurations and
RIO state. It releases both claims after verified restoration and
retains both if the restore readback fails.
[RP1 register reference](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)

Update `OUROSBOOT` with this package from PowerShell. Use the current
drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase15-gpio-loop-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive. With the Pi powered off, connect physical
header pin **11** (GPIO 17) to physical pin **13** (GPIO 27) through a
**1 kΩ series resistor**. Use only those two header pins for the test
connection. Keep the existing UART wiring and boot the Pi.
[Raspberry Pi 40-pin header pinout](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)

Run these commands from the automatic shell in PuTTY.

```text
exit
datacheck
resinfo
gpioinfo 17
gpioinfo 27
gpioloop
gpioloop
gpioinfo 17
gpioinfo 27
storagetest
resinfo
datacheck
appshell
echo gpio-loop-ok
```

Expect `PASS: GPIO 17 to 27 loopback levels 01010, both pins and claims restored.`
twice. Compare the GPIO settings and RIO outputs before and after, and
check eight claims, a passing USB read and the same `/data` generation.
In a separate run with the connection removed, `gpioloop` should
report the input mismatch and restore both pins and claims. Power off
the Pi before changing the connection.

The Pi reported a mismatch twice and restored GPIO 17, GPIO 27 and
all eight resource claims. USB readback passed and `/data` stayed at
generation 9. The shell echo confirmed the return to EL0. The first
capture lacks the failing step and the live signal at each pad.
[Pi loopback capture](../proofs/pi5-rp1-gpio-loop-2026-11-22.txt)

## Phase 15 GPIO pad and input diagnostics

`gpiopull <pin>` claims an isolated pin, enables its input buffer and
uses its internal pulls to verify `010` in both the raw pad status and
the RIO input register. The output remains disabled. The test restores
the pin function and pull before releasing its claim. The revised
`gpioloop` checks GPIO 17's actual pad input and output-enable status
before checking GPIO 27 for each level. A mismatch reports the step,
pin, expected level, pad level, RIO level and raw I/O status.
[RP1 register reference](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)

Apply the update to the current `OUROSBOOT` drive from PowerShell.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase15-gpio-pads-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the pendrive. Power off the Pi before removing any test
connection from GPIO 17 and 27. Boot with those two pins free and use
the existing UART wiring. Run these commands in PuTTY.

```text
exit
datacheck
resinfo
gpioinfo 17
gpioinfo 27
gpiopull 17
gpiopull 27
gpioinfo 17
gpioinfo 27
storagetest
resinfo
datacheck
appshell
echo gpio-pads-ok
```

Expect two `010` passes, matching GPIO snapshots, eight claims, a
passing USB read and the same data generation. For wired verification,
power off the Pi, connect physical header pins 11 (GPIO 17) and 13
(GPIO 27) through the 1 kΩ series resistor, and boot the same build.
From `user:/$`, run `exit`, then `gpioloop` and `resinfo`. A mismatch on
GPIO 17 identifies a source readback failure. A mismatch on GPIO 27
means the source readback passed that step. Compare `pad` and `rio`
in the mismatch line to distinguish an input-level mismatch from an
internal readback mismatch. [Pi header pinout](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)

GPIO 17 and 27 passed `010`, USB readback passed, and the eight claims
and generation 9 returned unchanged. The operator deferred the wired
retry until a series resistor is available. [Pi pad capture](../proofs/pi5-rp1-gpio-pads-2026-11-22.txt)

## Phase 16 USB device inventory

`usbdevices` reports the cached device and its validated configuration
after `storageinit` or `usbenum`. It lists the controller, root port,
slot, vendor and product IDs, interface classes and alternate settings,
and endpoint addresses, direction, transfer type, packet size and
`bInterval`. `storage-ready` reflects the last known driver state.
The command reads the cache and can be repeated while `/boot` and
`/data` use the pendrive. The configuration copy survives later reuse
of the xHCI transfer buffer. [USB configuration descriptors](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-configuration-descriptors)

The current enumerator captures a single direct USB2 device. The QEMU
keyboard check uses `scripts/test-usb-qemu.ps1 -Handoff -Keyboard`
to verify the HID boot-keyboard interface and its interrupt-IN endpoint.
Keyboard reports, hub traversal and hotplug are later increments.
[USB interface class codes](https://www.usb.org/defined-class-codes)

Apply the following boot-file update from PowerShell using the current
`OUROSBOOT` drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase16-usb-inventory-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive, boot the Pi from the usual pendrive, and start
at `user:/$` in PuTTY.

```text
exit
datacheck
resinfo
usbdevices
storagetest
usbdevices
resinfo
datacheck
appshell
echo usb-inventory-ok
```

Expect matching inventories with `state=storage-ready`, a `driver=usb-bot`
interface, and one bulk endpoint in each direction. The tested SanDisk
reports VID `0781`, PID `5567`, bulk IN `0x81` and bulk OUT `0x02`.
Confirm a passing storage test, eight claims and matching data generation
before and after.

The Pi reported the same SanDisk configuration and endpoints in three
inventories across two passing storage reads. The eight claims and
`/data` generation 9 were preserved.
[Pi inventory capture](../proofs/pi5-usb-inventory-2026-11-22.txt)

## Phase 16 live USB root ports

`usbports` reads the live xHCI root ports of the controller serving the
mounted USB drive. It reports device presence, port enable, protocol,
connection-change flag, raw `PORTSC` and which port belongs to `usb-bot`.
The command uses read-only register access and leaves change flags for
the controller's normal event path. Check `present` for current presence.
`usbdevices` continues to show cached descriptors. Class binding and
keyboard input on newly attached ports are later increments.
[xHCI port status register reference](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)

In PowerShell, use the current `OUROSBOOT` drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase16-usb-ports-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the pendrive, boot the Pi, and run these in PuTTY from `user:/$`.

```text
exit
datacheck
resinfo
usbports
storagetest
usbports
usbdevices
resinfo
datacheck
appshell
echo usb-ports-ok
```

Expect the pendrive's root port to show `present=yes enabled=yes` and
`owner=usb-bot` both times. The storage read should pass, and the claims
and mounted generation should match. QEMU acceptance additionally plugs
and unplugs a keyboard on the second root port and verifies reads while
it is attached and after removal.

## Phase 16 USB boot keyboard input

`usbkeytest <port>` selects a connected spare USB2 root port on the
active storage controller. It refuses the storage port before issuing
any device requests. On the spare port, it enumerates a temporary slot,
finds an alternate-zero HID boot keyboard interface, sets boot protocol,
and listens on its interrupt-IN endpoint for one key report. The keyboard
has separate endpoint rings and a separate output context. The command
disables its slot before returning to the monitor, including after a
timeout. This test prints a HID usage and modifier byte over UART. Normal
keyboard typing in the shell, use of the other controller, and hub
traversal are later work.
[USB HID boot protocol](https://www.usb.org/sites/default/files/hid1_11.pdf) ·
[xHCI endpoint and device-slot reference](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)

Use the boot-file updater from PowerShell with the current `OUROSBOOT`
drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase16-usb-keyboard-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive, boot the Pi with the pendrive connected and use
PuTTY to reach the monitor. Run these from `user:/$`.

```text
exit
datacheck
resinfo
usbports
```

Plug a wired USB boot keyboard directly into an unused USB socket,
leaving the boot pendrive plugged in. Run `usbports` again. Find a row
with `protocol=2 present=yes owner=none` (other fields may appear between
those labels). If no row changes to `present=yes`, try the keyboard in
another unused socket. Use the newly present root-port number in the
following commands. The example assumes it is port 2.

```text
usbkeytest 2
```

When the monitor shows `USB keyboard ready`, press **A** on the USB
keyboard. Expect usage `0x04`, modifiers `0`, and `keyboard slot released`.
Repeat `usbkeytest 2` without pressing a key to check timeout, then run
it once more and press **B** to check usage `0x05` and slot reuse.

```text
storagetest
usbdevices
resinfo
datacheck
appshell
echo usb-keyboard-ok
```

The boot drive should pass its sector read, its inventory and resource
claims should match, and both data checks should show the same generation.

## Phase 16 USB detection on both Pi hosts

The recovery image booted and the storage drive passed. The original
`usbports` reported only usb1, which showed a connection-change flag on
port 2 with `present=no`. This flag records a change since the last clear.
Current presence is reported separately.
[Pi capture](../proofs/pi5-usb-keyboard-port-2026-11-24.txt) ·
[Intel xHCI port status definitions](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)

`usbports` on Pi now shows `usb0` and `usb1`. Plug one accessory in at a
time after boot. Use the named `protocol=2 present=yes owner=none` row.
`usbidentify usb0 2` reads its device and interface descriptors, including
boot mouse or boot keyboard when declared. `usbkeytest usb0 2` reads a
key report. Substitute the row's host and port. The `usbkeytest 2` form
still targets port 2 on the mounted drive's controller. USB mouse pointer
input is outside this test.

The storage host uses a temporary device slot for the accessory. The
other host uses separately claimed registers and DMA pages, and releases
them after it halts. For initial testing connect the device directly and
leave the boot drive in place.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase16-usb-hosts-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

From the automatic shell use `exit`, then `usbports`. Run `usbports` again
while the keyboard or mouse remains plugged in. If usb0 port 2 is the
newly connected row, run `usbidentify usb0 2`. With the keyboard attached
run `usbkeytest usb0 2`, then press A on the USB keyboard at the prompt.
Finish with `storagetest`, `resinfo` and `datacheck`. The baseline on the
recovery image is eight claims and v5 generation 1.

## Phase 17 microSD detection and USB boot with accessories

The Pi reported a full-speed device on usb0 port 2 and the pendrive on
usb1 port 1. Boot stopped when the old storage probe chose usb0 first.
Storage initialization now searches the direct USB2 ports on both RP1
hosts for a BOT interface. It skips low-speed ports and releases the slot
of a device without BOT before continuing. When a scanned controller has
no candidate, the next controller can reuse the DMA memory after a verified
halt. A test with multiple USB mass-storage drives is outside this step.
[Pi capture](../proofs/pi5-usb-accessories-boot-selection-2026-11-24.txt)

`sdinfo` reports the onboard microSD host's presence, stability, power,
clock, busy and command queue status from read-only register accesses.
The existing firmware-handoff CMD17 path also invalidates its read session
if the card disappears or the controller reports removal. Reinsertion
requires a fresh firmware handoff. Full card initialization and write/flush
remain later steps. [SD host references](storage.md#primary-references)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase17-card-status-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot with the pendrive and keyboard attached and the microSD slot empty.
At `user:/$` enter `exit`, then run the monitor commands below. If the
keyboard's row is under usb0 and says port 2, use `usbidentify usb0 2`
and `usbkeytest usb0 2`. Press A on the USB keyboard when prompted.

```text
usbports
storagetest
datacheck
sdinfo
```

With a spare microSD card, insert it after USB boot and run `sdinfo`
again. Remove it and repeat `sdinfo`, `storagetest` and `datacheck`.
Check for `present=no`, `present=yes`, `present=no` and an unchanged
USB sector hash and `/data` generation.

## Phase 17 microSD board preflight

The Pi verified USB boot with the keyboard attached and read HID usages
`0x04` and `0x0c` on usb0 port 2. `sdinfo` reported a stable card while
the controller's power and clock were off.
[Pi capture](../proofs/pi5-usb-keyboard-live-sd-host-2026-11-24.txt)

`sdprobe` checks the Pi 5 device tree for the onboard SDHCI controller,
the associated GPIO controller and the card's VCC, I/O-voltage selector
and detect wiring. It reads host and GPIO registers once. The VCC enable
and voltage select fields describe GPIO state. They do not measure rail
voltage. The `present` and `stable` fields come from SDHCI, while
`cd_gpio_level` shows the raw active-low detect GPIO level.
[Pi 5 board device tree](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts) · [GPIO registers](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/gpio/gpio-brcmstb.c)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase17-sd-probe-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot from USB with the microSD slot empty. At the EL0 shell, enter `exit`.
At the monitor run `sdprobe`, then insert a spare microSD and run
`sdprobe` again. Run `storagetest` and `datacheck` to compare the USB
sector hash and extents generation with the original run. Save both SD
snapshots for the cold-start power and clock sequence.

## Phase 17 read a spare microSD after USB boot

On the Pi, insertion changed the detect GPIO from 1 to 0 while the SDHCI
presence bit stayed set. `sdprobe` now reports GPIO-based `present` and
the separate `host_present` value.
[Pi capture](../proofs/pi5-microsd-card-detect-2026-11-24.txt)

`sdtest` claims the onboard controller after USB boot, checks card detect
and the firmware's 3.3 V signals, then selects the SD pins and enables the
host at 400 kHz. It negotiates SDHC/SDXC, reads capacity and fingerprints
sector zero with CMD17. It restores host power, clock, interrupt controls
and pin selection. The test issues no media writes and leaves the card
unmounted. A restore failure retains the controller claim until reboot.
[SD specifications](https://www.sdcard.org/downloads/pls/) · [BCM2712 pin select](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/mmc/host/sdhci-brcmstb.c)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase17-sd-read-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot from USB with the microSD slot empty. Enter `exit` from `user:/$`
and run `sdtest` at `>`. It should report no card. Insert the spare card,
then run `sdprobe`, `sdtest`, `sdtest`, `sdprobe`, `storagetest`, and
`datacheck`. Compare the two sector hashes and check the card clock is
back off. A failed test reports the last command, IRQ and response.

### Phase 17 repeated microSD reads and host restoration

The Pi reached CMD17 transfer complete (`irq=0x2`, `response=0x900`).
The subsequent host restore check failed and the SD controller claim
stayed active. The following `sdprobe` showed power and clock off and
the original pin selection. The capture did not identify the register
behind the failed restore comparison.
[Pi CMD17 capture](../proofs/pi5-microsd-cmd17-restore-2026-11-24.txt)

The current driver waits for the command and data lines to go idle and
gates the SD clock before restoring the divider. Clock readback compares
the configuration fields and reset state. The read-only internal-clock
stable bit can change as the clock stops. Failures report the completed
read count and a restore bitmask. Bits `0x01`, `0x02`, `0x04`, `0x08`,
`0x10`, and `0x20` cover clock, host control, status enables, signal
enables, idle state, and pin selection, respectively.
[SDHCI clock register definitions](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/mmc/host/sdhci.h)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase17-sd-scan-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot from USB. At `user:/$`, enter `exit` to reach the monitor. With
the spare card inserted, run `sdtest`, `sdscan`, `sdscan`, `sdprobe`,
`storagetest`, and `datacheck`. `sdtest` reads one sector. Each `sdscan`
checks the first eight sectors with CMD17 and reports their combined
hash. Match the two scan hashes, check the clock is off, and compare
the USB sector hash and `/data` generation with the previous capture.

### Phase 17 restore SD power and verify the card capacity boundary

The Pi returned `read=1 restore_mask=0x02`. The SDHCI word at `0x28`
contains separate host control, power, block gap, and wake controls. The
new driver writes the host mode and power as individual bytes. It checks
the restored host mode and both power-enable bits after shutdown. The
voltage-select value is inactive with the bus off. A remaining control
mismatch prints `control0_before` and `after` to identify the bits.
[Pi capture](../proofs/pi5-microsd-control0-restore-2026-11-24.txt) · [SDHCI register definitions](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/mmc/host/sdhci.h)

`sdscan tail` reads the final eight sectors using the capacity from the
card's CSD. A native model verifies the last legal 32-bit sector address
on a card with 2³² sectors, separate byte restoration, and rejection if
power stays enabled. The test issues read commands and leaves the card
unmounted.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase17-sd-tail-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After USB boot enter `exit` from `user:/$`, insert the spare microSD,
and run the commands below at `>`. Match the two tail hashes and check
the reported LBA is eight below the capacity. Verify `sdprobe` shows
the host power and clock off and compare USB storage and `/data` with
the previous capture.

```text
sdtest
sdscan
sdscan tail
sdscan tail
sdprobe
storagetest
datacheck
```

## Phase 18 RP1 Ethernet PHY discovery

The Pi passed `sdtest`, `sdscan`, and two tail scans. A capacity of
125,173,760 sectors put the last eight at LBA 125,173,752. Both runs
returned `0x76efddc5`. The probe showed SD host power and clock off.
USB storage still returned `0xe4d803dc` and `/data` stayed at generation 1.
[Pi microSD capture](../proofs/pi5-microsd-tail-2026-11-24.txt)

`ethprobe` checks the known RP1 GEM and onboard PHY bindings and the live
PCIe window. It temporarily claims the GEM registers and reads PHY ID,
control and status over Clause 22 MDIO at PHY address 1. The second
status read reports current link after clearing the latched indication.
The probe restores the original MAC configuration after its reads and
leaves the PHY, transmit and receive engines, interrupts and DMA queues
in their existing state. Later steps cover PHY bring-up where firmware
left it inactive, then Ethernet frames and DHCP.
[RP1 clock and peripheral documentation](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf) · [Pi PHY binding](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts) · [GEM MDIO implementation](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/net/ethernet/cadence/macb_main.c)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-probe-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Enter `exit` from `user:/$` and run `ethprobe` at `>` with Ethernet
unplugged. A responding PHY should report an ID and `link=down`. If an
Ethernet cable and switch or router are available, connect them, wait
for link negotiation and repeat `ethprobe`. Then run `resinfo`,
`storagetest`, and `datacheck`. The probe's claim should disappear
after a verified MAC restore.

### Phase 18 onboard PHY reset diagnostic

The Pi identified the GEM as `0x00070109`, but its PHY gave no MDIO
response. Windows identified the hub as `Ethernet 3`, a Realtek USB
Ethernet adapter, and reported no live link at the time of the check.
[Pi and Windows capture](../proofs/pi5-ethernet-mdio-no-reply-2026-11-24.txt)

`ethprobe reset` runs the normal probe first. If the PHY gives no reply,
it temporarily claims the device-tree-defined active-low PHY reset line
on RP1 GPIO32. It verifies a 5 ms low pulse, then a high release, waits
100 ms, repeats MDIO, and restores the pin state. The Ethernet link
reading during that retry is an early sample. For the link check, wait a
few seconds with the cable attached and run `ethprobe` again. A failed
restore keeps the claims until reboot.
[Pi 5 reset binding](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts) · [RP1 GPIO bank map](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/pinctrl/pinctrl-rp1.c)

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-reset-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

With the Ethernet cable connected directly from the Pi to the laptop's
adapter, enter `exit` from `user:/$`, then run `ethprobe reset` at `>`.
Wait a few seconds and run `ethprobe`, `resinfo`, `storagetest`, and
`datacheck`. `rp1-ethernet` is the resource owner name shown by `resinfo`.

### Phase 18 hold the PHY out of reset for link negotiation

On the Pi, `ethprobe reset` read PHY ID `0x600d84a2` with negotiation
pending. The next probe read `0xffffffff` after GPIO32 was restored.
The Windows adapter still showed no link. USB reads passed and `/data`
remained at generation 1. [Pi reset capture](../proofs/pi5-ethernet-phy-reset-2026-11-24.txt)

`ethup` verifies idle GEM access, pulses the board's PHY reset, and
holds GPIO32 high after the PHY replies. `ethprobe` reports fresh link
and negotiation status without pulsing reset while the pin is held.
`ethdown` restores the saved GPIO configuration. `resinfo` shows two
`rp1-ethernet` claims during `held=yes` and releases them after `ethdown`
verifies restoration. The MAC transmit and receive engines stay idle.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-link-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Connect the Pi directly to the laptop's USB Ethernet adapter. Enter
`exit` from `user:/$`, then run `ethup` at `>`. Wait a few seconds for
negotiation, run `ethprobe` and `resinfo`, and check link from Windows
before issuing `ethdown`.

```powershell
Get-NetAdapter -Name 'Ethernet 3' | Select-Object Name,Status,LinkSpeed
```

Run `ethdown`, `resinfo`, `storagetest`, and `datacheck` in PuTTY to
verify cleanup and compare storage with the previous capture.

The Pi reported link up and auto-negotiation complete while the PHY was
held. The Windows adapter reported 100 Mbps. After `ethdown`, the Pi
returned to eight resource claims, the USB read hash matched the earlier
run, and `/data` remained at generation 1.
[Pi and Windows capture](../proofs/pi5-ethernet-link-2026-11-24.txt)

### Phase 18 Ethernet MAC DMA loopback

`ethloop` runs a bounded internal Cadence GEM loopback. It uses queue zero,
two legacy descriptors per direction, and one page of Normal-NC DMA memory
claimed alongside the MAC. The test checks descriptor ownership, frame
length and contents, and RX buffer guards. It stops the MAC and restores
the saved configuration before returning the page and claims. If completion
or restoration cannot be verified, the page and claims remain reserved until
reboot. Hardware frame counters may advance while the test runs.
[RP1 peripheral documentation](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)

Apply the boot package to the existing `OUROSBOOT` partition.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-loop-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot from the updated USB drive with Ethernet unplugged. At the PuTTY
`user:/$` prompt run `exit`. At the `>` prompt run `ethloop`. After a
PASS, repeat `ethloop`, then run `resinfo`, `storagetest`, and
`datacheck`. Expect two matching passes, eight claims, the prior USB
hash, and the same mounted data generation. Capture the full `ethloop`
output on failure. Reboot if the command reports that it retained the
DMA page.

On the Pi, three `ethloop` runs passed. Resource claims returned to eight,
the USB hash matched, and `/data` remained at generation 1.
[GEM loopback capture](../proofs/pi5-ethernet-loopback-2026-11-24.txt)

### Phase 18 Ethernet ARP over the cable

`etharp` reads the onboard PHY's negotiated full-duplex speed while
`ethup` holds it out of reset. It programs the GEM speed and unicast
filter using the board's RGMII routing and sends a broadcast ARP request from
`192.0.2.2` to Windows at `192.0.2.1`. It checks the full ARP reply
address tuple and prints the peer MAC. The command restores the MAC
configuration after a completed exchange. An unverified DMA completion
keeps the page reserved until reboot.
[RP1 GEM configuration](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/ethernet/cadence/macb_main.c)

The first cable test resolved 100 Mbps, but both attempts stopped during
MAC setup with `result=2`. The output contained no descriptor completion
and did not identify the failed register. The updated test leaves the
generic GEM `USRIO` selector as configured by the firmware. It prints the
register offset, expected value, and readback when a configuration
write fails verification.
[ARP capture](../proofs/pi5-ethernet-arp-setup-2026-11-24.txt)

The next run read the PHY ID with the link down and negotiation
pending. `etharp` stopped at the link check. USB and data checks passed.
[Pending link capture](../proofs/pi5-ethernet-link-pending-2026-11-24.txt)

`ethrecover` can be used after `ethup` when negotiation stalls. It
requests one Clause 22 restart on the held BCM54213PE with auto-negotiation
enabled and polls status for about four seconds. A link that is already
up and negotiated is left alone. The command restores the GEM management
registers before reporting the current status.
[Clause 22 definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/uapi/linux/mii.h)

Apply the recovery package to the existing USB boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-recovery-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot the Pi and connect its Ethernet port directly to the laptop's USB
Ethernet adapter. At `user:/$` enter `exit`. At `>` run `ethup`, wait
a few seconds, and run `ethprobe`. If it still reports `link=down`,
reseat the cable at both ends and run `ethrecover`. Check `Ethernet 3`
in Windows while `held=yes`. Proceed when the Pi reports
`link=up autoneg=complete` and Windows reports `Up`.

Run the following in an administrator PowerShell on Windows. Keep the
window open so the saved DHCP state is available for cleanup. Wait for
the address state to be `Preferred` before running the Pi test.
[Windows address configuration](https://learn.microsoft.com/en-us/powershell/module/nettcpip/new-netipaddress?view=windowsserver2025-ps)

```powershell
$nic = 'Ethernet 3'
Get-NetAdapter -Name $nic | Select-Object Name,Status,LinkSpeed,MacAddress
if (!(Get-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 -ErrorAction SilentlyContinue)) {
    $dhcp = (Get-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4).Dhcp
    New-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 -PrefixLength 30 -PolicyStore ActiveStore
}
Get-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 | Select-Object IPAddress,AddressState
```

Run `etharp` at `>`. Expect `PASS: ARP reply from 192.0.2.1` and the
Windows adapter MAC. After a pass, repeat `etharp`, then run `ethdown`,
`resinfo`, `storagetest`, and `datacheck`. Expect eight claims and the
previous storage hash and generation. On failure, include the entire
report with any `reg`, `expected`, and `observed` values. Reboot if the
report says the DMA page was retained.

In the same administrator PowerShell, remove the address created for
the test and restore DHCP if it was enabled.

```powershell
Remove-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 -PolicyStore ActiveStore -Confirm:$false
if ($dhcp -eq 'Enabled') {
    Set-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4 -Dhcp Enabled -PolicyStore ActiveStore
}
```

The Pi received the ARP reply at 100 Mbps from Windows
(`20-7B-D5-1A-C0-91`). After `ethdown`, resource claims returned to eight,
the USB read hash matched, and `/data` remained at generation 1.
[Successful cable ARP capture](../proofs/pi5-ethernet-arp-pass-2026-11-24.txt)

### Phase 18 IPv4 echo over Ethernet

`ethping` uses the held onboard Ethernet interface. It resolves the
Windows MAC with ARP and sends one IPv4 ICMP Echo Request from
`192.0.2.2` to `192.0.2.1`. The reply must match both MAC and IPv4
addresses, the echo identifier, sequence and payload. The IPv4 header
and ICMP checksums must both verify. Each exchange restores the GEM
before sharing the scratch page with the next transfer. A failure
identifies the ARP or ICMP step.
[ICMP Echo format](https://www.rfc-editor.org/rfc/rfc792.html)

Apply the IPv4 update to the existing USB boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-ipv4-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot the Pi with the Ethernet cable connected. At the PuTTY
`user:/$` prompt enter `exit`. At `>` run `ethup` and then
`ethprobe`. If the link stalls, run `ethrecover`. While the PHY is
held, confirm in Windows that `Ethernet 3` is up and the test address
is preferred, using the address setup above if needed.

```powershell
Get-NetAdapter -Name 'Ethernet 3' | Select-Object Status,LinkSpeed
Get-NetIPAddress -InterfaceAlias 'Ethernet 3' -IPAddress 192.0.2.1 |
    Select-Object IPAddress,AddressState
```

In administrator PowerShell, allow incoming echo requests only on the
direct USB Ethernet link between the two test addresses.
[Windows firewall rule documentation](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule?view=windowsserver2025-ps)

```powershell
New-NetFirewallRule -Name 'OurOS-Ethernet-ICMP-Test' -DisplayName 'OurOS Ethernet ICMP test' `
    -Direction Inbound -Action Allow -Protocol ICMPv4 -IcmpType 8 `
    -InterfaceAlias 'Ethernet 3' -LocalAddress 192.0.2.1 -RemoteAddress 192.0.2.2
```

At the Pi monitor run `ethping`. Expect `PASS: ICMP echo reply from
192.0.2.1` and the Windows adapter MAC. After a pass, repeat `ethping`
then run `ethdown`, `resinfo`, `storagetest`, and `datacheck`. If the
diagnostic reports DMA retention, capture the full output and reboot
before retrying.

Remove the temporary rule in administrator PowerShell.

```powershell
Remove-NetFirewallRule -Name 'OurOS-Ethernet-ICMP-Test'
```

Both Pi runs received the matching IPv4 echo reply from Windows at
100 Mbps. After releasing the PHY, eight resource claims remained, the
USB hash matched, and `/data` remained at generation 1.
[Wired IPv4 capture](../proofs/pi5-ethernet-ipv4-pass-2026-11-24.txt)

### Phase 18 UDP sockets over Ethernet

`ethudpup` activates the existing UDP socket service on the held
Ethernet link using the direct-test address `192.0.2.2/30`. It selects
the interface explicitly and uses the same cooperative worker and EL0
socket ABI as Wi-Fi. The worker shares the PHY hold's GEM claim and
acquires one DMA page for the transmit and receive rings. It exchanges
ARP and UDP frames while the monitor or user shell runs. `ethdown`
stops the worker, invalidates UDP sockets, restores the GEM and
releases the PHY. An unverified DMA stop retains the relevant resources
until boot. Ethernet uses the single active network worker. The cable
test covers outbound IPv4 UDP to the on-link Windows peer.
[Cadence GEM ring handling](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/ethernet/cadence/macb_main.c)

Apply the UDP update to the existing USB boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-udp-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot the Pi with the cable connected. At `user:/$` type `exit`. Run
`ethup` and wait for `ethprobe` to show `link=up autoneg=complete`.
Run `ethudpup` at `>`, then `appshell` after it reports that UDP is
active. Use the address setup above if Windows needs `192.0.2.1/30`.

Run the following in administrator Windows PowerShell from the project
folder. The firewall rule applies to UDP port 4040 on the direct cable.
[Windows firewall rule reference](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule?view=windowsserver2025-ps)

```powershell
New-NetFirewallRule -Name 'OurOS-Ethernet-UDP-Test' -DisplayName 'OurOS Ethernet UDP test' `
    -Direction Inbound -Action Allow -Protocol UDP -LocalPort 4040 `
    -InterfaceAlias 'Ethernet 3' -LocalAddress 192.0.2.1 -RemoteAddress 192.0.2.2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/udp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4040
```

With Windows waiting, run this at `user:/$`.

```text
run /boot/UDPCHK.ELF 192.0.2.1 4040 wired-udp
```

Expect Windows to echo nine bytes and the app to print `PASS: UDP
echo matched the sent datagram.` Start the Windows echo script again
for a second test in the same Pi session. Then type `exit` and run
`ethdown`, `resinfo`, `storagetest` and `datacheck` at `>`.
Expect eight resource claims and the prior storage hash and generation.
Remove the temporary Windows firewall rule after the test.

```powershell
Remove-NetFirewallRule -Name 'OurOS-Ethernet-UDP-Test'
```

Windows echoed nine bytes at port 4040, and `UDPCHK.ELF` confirmed
the contents. An earlier attempt used port 4 and timed out. Resource
claims, the USB hash, and the data generation returned to their
baseline after `ethdown`.
[Wired UDP capture](../proofs/pi5-ethernet-udp-pass-2026-11-24.txt)

### Phase 18 TCP streams over Ethernet

`ethtcpup` enables the existing TCP and UDP socket APIs on the held
Ethernet interface at `192.0.2.2/30`. The worker alternates which
protocol gets first use of the shared transmit descriptor. TCP takes
its session secret from the Pi's hardware RNG, and `ethdown` closes
both socket services before releasing GEM DMA and the PHY. Use
`ethudpup` to start a UDP-only session. TCP uses active connections to
on-link IPv4 peers.
[TCP specification](https://www.rfc-editor.org/rfc/rfc9293.html)

Apply the TCP update to the USB boot volume. `LARGE.BIN` is the existing
16 KiB input file used by the echo application.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-tcp-boot-update.zip'
Get-Item -LiteralPath D:\LARGE.BIN | Select-Object Name,Length
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Boot with the Ethernet cable attached. In PuTTY, type `exit` to reach
`>`, run `ethup`, and check for `link=up autoneg=complete` with
`ethprobe`. Run `ethtcpup` and then `appshell` once the monitor reports
`Ethernet TCP and UDP active at 192.0.2.2/30`.

In administrator Windows PowerShell, with `192.0.2.1` preferred on
`Ethernet 3`, allow TCP port 4041 only between the test addresses and
start the echo service from the project folder.
[Windows firewall rule reference](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule?view=windowsserver2025-ps)

```powershell
New-NetFirewallRule -Name 'OurOS-Ethernet-TCP-Test' -DisplayName 'OurOS Ethernet TCP test' `
    -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4041 `
    -InterfaceAlias 'Ethernet 3' -LocalAddress 192.0.2.1 -RemoteAddress 192.0.2.2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4041
```

When Windows says `Waiting`, run this at the OurOS `user:/$` prompt.

```text
run /boot/TCPCHK.ELF 192.0.2.1 4041
```

Expect both sides to report 16384 bytes with identical SHA-256 values.
Type `exit`, then run `ethdown`, `resinfo`, `storagetest`, and
`datacheck` at `>`. The baseline is eight resource claims and the
previous USB hash and data generation. Remove the temporary firewall
rule in the administrator PowerShell.

```powershell
Remove-NetFirewallRule -Name 'OurOS-Ethernet-TCP-Test'
```

The Windows echo server and `TCPCHK.ELF` both reported 16,384 bytes and
the same SHA-256 on port 4041. Two attempts on port 4 timed out.
[Wired TCP capture](../proofs/pi5-ethernet-tcp-pass-2026-11-24.txt)

### Phase 18 Configurable Ethernet IPv4 and inbound reachability

- `ethaddr` displays the wired address. `ethaddr IPv4/prefix` sets a
  unicast host address on a directly connected subnet with prefix 1–30.
  It defaults to `192.0.2.2/30` at boot. Stop an active session with
  `ethdown` before changing the address.
- `ethudpup` and `ethtcpup` answer ARP requests for the configured
  address, including broadcast requests. They also answer unfragmented
  IPv4 ICMP Echo requests from an on-link peer, with up to 524 payload
  bytes and a 20-byte IPv4 header.
- ARP and ICMP replies each have a 100 ms transmit interval and use the
  shared DMA transmit descriptor when available. Both socket services
  use the configured subnet. The `etharp` and `ethping` diagnostics use
  the fixed `192.0.2.2` and `192.0.2.1` test addresses.
  [ARP](https://www.rfc-editor.org/rfc/rfc826.html) · [ICMP](https://www.rfc-editor.org/rfc/rfc792.html)

The native GEM fixture verifies replies on `192.0.2.6/30`, checksums,
oversized and fragmented input rejection, reply rate limits, and DMA
cleanup. QEMU passes the monitor command routing, UDP and TCP socket
checks, and USB boot with a keyboard. The cable test below is the Pi
acceptance check.

Apply the boot-file update from Windows PowerShell in the project folder.
The preview on the prior TCP package shows `Replace OUROS.IMG`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase18-ethernet-address-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the USB drive, then boot the Pi with Ethernet attached.
In PuTTY, type `exit` at `user:/$`. Run these at the `>` prompt.

```text
ethaddr
ethaddr 192.0.2.6/30
ethup
ethprobe
```

Repeat `ethprobe` until it reports `link=up autoneg=complete`. Run
`ethtcpup` and confirm `Ethernet TCP and UDP active at 192.0.2.6/30`.
Running `ethaddr 192.0.2.2/30` during the session should ask you to run
`ethdown` first. Running `ethaddr` should still show `192.0.2.6/30`.

In administrator Windows PowerShell, add a temporary address on the
direct cable and wait for `AddressState` to show `Preferred`.
Adding a static address can disable DHCP on that adapter.
[Windows address configuration](https://learn.microsoft.com/en-us/powershell/module/nettcpip/new-netipaddress?view=windowsserver2025-ps)

```powershell
$nic = 'Ethernet 3'
$dhcp = (Get-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4).Dhcp
New-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.5 -PrefixLength 30 -PolicyStore ActiveStore
Get-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.5 | Select-Object IPAddress,AddressState
```

Send three 512-byte pings. The neighbor entry should show
`02-00-00-00-18-02` for `192.0.2.6`.

```powershell
ping.exe -n 3 -l 512 192.0.2.6
Get-NetNeighbor -InterfaceAlias $nic -IPAddress 192.0.2.6 | Select-Object IPAddress,LinkLayerAddress,State
```

On the Pi, run these at `>`. Expect eight claims, the prior USB hash
and the same mounted data generation.

```text
ethdown
resinfo
storagetest
datacheck
ethaddr 192.0.2.2/30
```

In the same administrator PowerShell, remove the temporary address.

```powershell
Remove-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.5 -PolicyStore ActiveStore -Confirm:$false
if ($dhcp -eq 'Enabled') { Set-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4 -Dhcp Enabled -PolicyStore ActiveStore }
```

The Pi started at `192.0.2.6/30` with 100 Mbps full duplex. Windows
received six 512-byte replies with zero loss and then showed neighbor
`02-00-00-00-18-02` as reachable. The first neighbor query ran before
the ping populated the cache. The Pi returned to eight claims, the USB
read hash matched, and `/data` stayed at generation 1.
[Wired IPv4 configuration capture](../proofs/pi5-ethernet-address-pass-2026-11-24.txt)

## Phase 19 external PCIe

`pcieinfo` reports the external PCIe connector's device-tree status,
domain, controller register base, lane count, and configured maximum
generation. `pcieinfo rp1` reports the internal port for comparison.
The configured generation and lane count describe the device tree.
[Pi 5 PCIe connector](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#raspberry-pi-connector-for-pcie)

`pciewin` and `pciewin rp1` report the outbound CPU-to-PCI windows and
inbound PCI-to-CPU DMA windows for each port. They validate address
widths, span arithmetic, overlap, and the upstream AXI translation.
An inbound table can describe an MSI doorbell alongside RAM. Both
commands read the firmware device tree and can run on a Pi booted
from USB. Live link training, endpoint enumeration, and NVMe queues
are later Phase 19 steps.
[BCM2712 port definitions](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)

The native checks pass on the pinned device tree and reject malformed
windows. Both QEMU profiles exercise command routing and the USB storage
regression passes. The header and debug builds each have 4 KiB before
the firmware DTB. The updater preview against the Phase 18 files shows
`Replace OUROS.IMG`.

From Windows PowerShell in the project folder, apply the package to
the existing USB boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase19-pcie-inventory-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. Type `exit` in the EL0 shell to reach the
monitor. Run the following commands one at a time.

```text
pcieinfo
pcieinfo rp1
pciewin
pciewin rp1
resinfo
storagetest
datacheck
```

The default USB boot device tree describes the external port as
`dt=disabled domain=1` with one lane. RP1 should show
`dt=enabled domain=2`. The pinned tree has two outbound windows per
port, two inbound windows for the external port, and three inbound
windows for RP1. Finish by confirming eight resource claims, the USB
read PASS, and the current `/data` generation.

The Pi hardware run reported the expected external and RP1 window
counts and addresses. USB readback passed, eight claims remained, and
`/data` matched bank 0 at generation 1.
[PCIe inventory capture](../proofs/pi5-pcie-inventory-pass-2026-11-24.txt)

## Phase 20 RTC and power

| Command | Behavior |
| --- | --- |
| `rtcset <YYYY-MM-DDTHH:MM:SSZ>` | Sets the hardware RTC and verifies it with a separate readback. Accepted values run from 2000-01-01 through 2106-02-07T06:28:15Z. |
| `rtcsync` | Explicitly imports the RTC into the current OurOS UTC clock. UTC reports `source=rtc` and retains the existing 24-hour freshness limit and backward-time check. |
| `rtcalarm` | Reads the firmware alarm timestamp and enable state. |
| `powerinfo` | Shows the raw word and current and historical undervoltage, frequency capping, throttling, and soft temperature limit flags. |
| `poweroff` | Stops the network worker, flushes mounted `/data`, quiesces storage, and requests a firmware halt on Pi or PSCI system off on QEMU. |

The RTC write and software UTC import are separate operator actions.
Alarm setup and wake testing are later work. The Pi's off state depends
on EEPROM configuration. For this test, keep USB and power connected
through the halt and use the Pi's power button to boot it again.
[RTC firmware registers](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/rtc/rtc-rpi.c) · [Throttle flags](https://www.raspberrypi.com/documentation/computers/os.html#get_throttled) · [Firmware halt path](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/watchdog/bcm2835_wdt.c)

Native checks pass for RTC reply handling, UTC source and backward-time
handling, and the storage shutdown sequence. Both QEMU profiles pass
reboot and exit cleanly on poweroff with the mounted disk hash intact.
Both Pi builds have 4 KiB of room before the firmware device tree.
The verified updater preview against Phase 19 replaces `OUROS.IMG`.

Apply the boot-file package from Windows PowerShell in the project
folder, with the boot volume mounted as `D:`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase20-rtc-power-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. Type `exit` to reach the monitor, then
check the existing RTC and `/data` baseline.

```text
rtcinfo
utc
datacheck
```

Generate a fresh RTC command in Windows PowerShell.

```powershell
"rtcset " + [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
```

Run the line printed by PowerShell in PuTTY. Expect
`RTC set verified unix_seconds=`. Then run these monitor commands.

```text
rtcinfo
utc
rtcsync
utc
rtcalarm
powerinfo
```

On a fresh boot, the first `utc` remains unset until `rtcsync`.
The second `utc` should show `source=rtc` and a recent UTC value.
Once those checks finish, run the following.

```text
poweroff
```

Expect `Poweroff: storage synchronized and idle. Halt via BCM2712
watchdog.` After the Pi halts, press its power button. Type `exit`
again in the new shell, then run these commands.

```text
rtcinfo
datacheck
storagetest
```

Compare the RTC and data generation with the pre-shutdown readings.
The USB storage test should pass.

The Pi verified the RTC write, used the RTC as its UTC source, and
reported all throttle flags clear. It issued the storage-synchronized
halt request and subsequently booted with the RTC advancing and the
same valid `/data` generation. Both alarm reads returned an invalid
firmware response.
[Phase 20 hardware capture](../proofs/pi5-rtc-power-2026-11-24.txt)

## Phase 21 CPU and cache diagnostics

- `cpulist` decodes CPU affinities and availability from the device
  tree and identifies the CPU executing the monitor.
- `cpustate` checks the firmware's PSCI affinity state for eligible
  processors. The firmware can report on, off, pending, or unavailable.
  [Arm PSCI specification](https://documentation-service.arm.com/static/6703a8b8d7e4b739d817e10d)
- `cacheinfo` shows the current EL1 instruction and data cache enable
  bits, the smallest line sizes, and the architecture's cache
  maintenance geometry. It restores the previous cache selector after
  each read. [Arm Cortex-A76 register reference](https://documentation-service.arm.com/static/5f562083235b3560a01e03bc)
- `atomictest` exercises acquire/release, successful and failed
  compare-exchange, and fetch-add on one aligned Normal-NC word owned
  by the boot CPU. The test runs after MMU initialization.
- The `rtcalarm` read accepts a missing selector echo and extracts the
  alarm enable bit. It reports response fields on an invalid reply to
  help identify any remaining firmware format differences.
  [Raspberry Pi RTC driver](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/rtc/rtc-rpi.c)

This increment prepares for secondary-core startup. The monitor runs on
the boot CPU, and enabling caches and starting secondary cores are
later steps. The Pi linker groups the strictest-aligned BSS sections
first to recover padding while preserving page alignment and the
firmware device-tree boundary.

Both Pi kernel profiles fit with 8 KiB before the firmware device tree.
The updater preview against a copy of the Phase 20 boot files selects
`OUROS.IMG`.
Four-core QEMU runs on Cortex-A53 and Cortex-A76 enumerated the boot
CPU and three off cores, reported cache geometry, and passed repeated
atomic tests and the USB and data checks. The Pi's reported firmware
states and alarm response are hardware acceptance checks.

Update the existing boot volume from Windows PowerShell.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase21-cpu-platform-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. Type `exit` at `user:/$`, then run these at
the monitor prompt.

```text
cpulist
cpustate
cacheinfo
atomictest
rtcalarm
resinfo
storagetest
datacheck
```

The pinned Pi device tree contains affinities `0x0`, `0x100`,
`0x200`, and `0x300`. The running core is marked `current`. Record
the firmware's reported states, cache geometry, and the alarm reply.
Expect `Cache I=off D=off`, an atomic-test PASS, eight resource claims,
the same USB hash, and the same data generation.

The Pi listed all four affinities and PSCI reported the three
secondary cores off. Cache diagnostics showed 64-byte lines and an L3
cache. `atomictest` halted on `LDAXR` under the uncached profile. The
syndrome identifies a synchronous external data abort and marks FAR
invalid. The alarm command was pending when the monitor halted.
[Phase 21 Pi capture](../proofs/pi5-cpu-atomic-abort-2026-11-24.txt) · [Arm exception syndrome](https://support.arm.com/documentation/111107/2026-09/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-)

## Phase 22 Media inventory and WAV tools

On the Pi, `atomictest` now verifies ordered loads and stores with
`LDAR` and `STLR`. Exclusive read-modify-write testing on Pi needs a
cacheable test mapping. QEMU retains the compare-exchange and fetch-add
checks.

| Feature | Usage |
| --- | --- |
| Media inventory | `mediainfo` at the kernel monitor lists firmware-described display, camera, audio, and graphics controllers and their effective device-tree status. |
| WAV metadata | `/boot/WAVINFO.ELF <file>` validates a RIFF WAVE file and prints channels, sample rate, bit depth, frames, and duration. |
| Sample statistics | `/boot/WAVINFO.ELF --stats <file>` adds per-channel minimum, maximum, peak magnitude, and the number of samples at either PCM rail. Use `-` to read binary stdin. |
| Tone generation | `/boot/WAVTONE.ELF <Hz> <ms> > <new-file>` creates a 16-bit mono triangle wave at 8 kHz. Frequency is 20–2000 Hz and duration is 1–250 ms. |

WAVINFO supports mono or stereo integer PCM at 8 or 16 bits and handles
odd-length chunk padding. The parser streams data in bounded blocks
and supports RIFF payloads up to 16 MiB. Playback, capture, and display
scanout drivers are later Phase 22 steps.
[Microsoft RIFF layout](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-) · [WAVEFORMATEX](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex)

The Pi object's disassembly shows only the acquire/release loads and
stores in the revised test. The same Pi diagnostic branch passed twice
in a QEMU handoff test. The media suite passed on both QEMU profiles,
including file and pipeline input, the odd RIFF padding case, malformed
files, and USB and data validation. The updater replaces `OUROS.IMG`
and adds `WAVINFO.ELF` and `WAVTONE.ELF` to the Phase 21 boot files.

Apply the file update from Windows PowerShell in the project folder.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase22-media-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After boot, type `exit` at `user:/$` and run these at the monitor.

```text
atomictest
atomictest
mediainfo
rtcalarm
appshell
```

The Pi should print two acquire/release PASS lines. Capture the alarm
response from this boot.

At `user:/$`, test the WAV tools in the temporary filesystem.

```text
/boot/WAVTONE.ELF 400 200 > /tmp/phase22.wav
/boot/WAVINFO.ELF /tmp/phase22.wav
/boot/WAVINFO.ELF --stats /tmp/phase22.wav
rm /tmp/phase22.wav
exit
```

Expect `PCM channels=1 rate_hz=8000 bits=16 frames=1600 duration_ms=200`
and `channel=1 min=-16000 max=16000 peak=16000 clipped=0`.
At the monitor, run `resinfo`, `storagetest`, and `datacheck`.

The Pi passed three corrected atomic tests, listed its firmware media
devices, read the disabled RTC alarm, and reported the expected WAV
metadata and sample statistics. The monitor still reported eight resource
claims, the same USB storage hash, and mounted data generation 1.
[Phase 22 Pi capture](../proofs/pi5-media-pass-2026-11-24.txt)

## Phase 23 PCM WAV editing

`WAVEDIT.ELF` adds five operations for mono and stereo 8-bit and 16-bit
integer PCM. It validates each input before emitting a WAV with the
resulting format and samples.

| Operation | Arguments | Result |
| --- | --- | --- |
| Trim | `trim <start-ms> <length-ms> <file>` | Extracts a nonempty range of whole frames. |
| Gain | `gain <percent> <file>` | Scales samples by 0–400 percent and saturates at the PCM limits. |
| Mono | `mono <file>` | Averages the two channels of a stereo file and passes mono samples through. |
| Fade | `fade <in-ms> <out-ms> <file>` | Ramps the beginning and end of the track linearly. Zero disables the corresponding ramp. A one-frame ramp silences that frame. |
| Join | `join <left> <right>` | Concatenates files with equal sample rate, bit depth, and channel count. |

Time values round down to whole frames. Trim ranges must fit the input
and fades must fit without overlapping. Use ordinary files as inputs
and redirect the binary output to a new path. The output contains the
PCM format and audio data, with a final pad byte when needed. Invalid
operands and I/O errors return exit code 2. The shell may leave an empty
or incomplete output file after a failed edit. The mount's file-size limit applies
to the finished WAV, including its header and padding.
[RIFF chunk layout](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-)

`WAVTONE.ELF` accepts an optional third argument for the right-channel
frequency, which lets you test stereo editing without any accessories.
The Phase 23 updater adds `WAVEDIT.ELF` and replaces `WAVINFO.ELF` and
`WAVTONE.ELF` on a Phase 22 boot volume.

The QEMU media suite passed on Cortex-A53 and Cortex-A76. It compared all
five edits byte for byte with expected files, verified 16-bit stereo
downmix and clipping, and checked invalid ranges, incompatible inputs,
and truncated files. The updater passed preview, apply, and a second
preview against a copy of the previous boot files.

In Windows PowerShell from the project directory, preview and apply the
update to the pendrive's boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase23-wav-edit-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, enter these commands.

```text
/boot/WAVTONE.ELF 400 100 800 > /tmp/p23-src.wav
/boot/WAVEDIT.ELF trim 25 50 /tmp/p23-src.wav > /tmp/p23-trim.wav
/boot/WAVEDIT.ELF gain 150 /tmp/p23-src.wav > /tmp/p23-gain.wav
/boot/WAVEDIT.ELF mono /tmp/p23-src.wav > /tmp/p23-mono.wav
/boot/WAVEDIT.ELF fade 25 25 /tmp/p23-src.wav > /tmp/p23-fade.wav
/boot/WAVEDIT.ELF join /tmp/p23-mono.wav /tmp/p23-mono.wav > /tmp/p23-join.wav
/boot/WAVINFO.ELF /tmp/p23-trim.wav
/boot/WAVINFO.ELF --stats /tmp/p23-gain.wav
/boot/WAVINFO.ELF --stats /tmp/p23-mono.wav
hexdump -s 44 -n 4 /tmp/p23-fade.wav
hexdump -s 3240 -n 4 /tmp/p23-fade.wav
/boot/WAVINFO.ELF /tmp/p23-join.wav
rm /tmp/p23-src.wav
rm /tmp/p23-trim.wav
rm /tmp/p23-gain.wav
rm /tmp/p23-mono.wav
rm /tmp/p23-fade.wav
rm /tmp/p23-join.wav
exit
```

Expected results are 400 trim frames, 24,000 peak on each gain channel,
800 mono frames with one channel, four zero bytes at each fade endpoint,
and 1,600 joined frames. At the monitor, run `resinfo`, `storagetest`,
and `datacheck` to compare with the Phase 22 baseline.

The Pi generated the stereo tone and verified the gain and downmix
statistics. The trim, fade, and join attempts had altered paths or
missing arguments in the serial transcript. The resource and storage
checks passed. The Phase 24 script exercises all five edits on the Pi
with one command. [Phase 23 Pi capture](../proofs/pi5-wav-serial-retest-2026-11-24.txt)

## Phase 24 Serial diagnostics and batch verification

| Feature | Behavior |
| --- | --- |
| UART status | `uartinfo` at the kernel monitor reports PL011 FIFO status, received bytes, and observed framing, parity, break, and overrun errors. |
| Input rejection | A reported receive fault discards the damaged line through Enter. The monitor, EL0 terminal, and foreground Wi-Fi prompt report the rejection. Hidden input is wiped. |
| Monitor editing | Ctrl+C cancels the current line. Ctrl+U clears it. |
| Batch execution | `source -e PATH` stops at the first line with nonzero final status and prints its line number. `source PATH` retains its existing behavior. |
| File verification | `sha256 PATH [expected-hex]` can compare a file against a 64-digit SHA-256. A mismatch returns 1 and malformed hex returns 2. |

The PL011 driver checks for hardware overruns before accepting a queued
byte. Its status counts the errors it observes since boot. Hardware can
only report faults it detects. [Arm PL011 reference](https://documentation-service.arm.com/static/5e8e36c2fd977155116a90b5)

`/boot/WAVCHECK.OSH` generates the stereo tone, applies all five edits,
verifies all six file hashes, and removes its temporary directory on
success. Run it with `source -e`. The script reserves `/tmp/wavcheck`
for that run. If a run stops after creating it, reboot the Pi to clear
the temporary files before retrying.

The Cortex-A53 and Cortex-A76 QEMU profiles each verified two complete
WAV runs, a batch that stopped at an existing directory, and recovery
from a simulated UART break at each prompt. The host tests verified
hidden-input wiping and foreground Wi-Fi prompt recovery. Preview,
apply, and rollback passed on copied Phase 23 boot files.

From the project folder in Windows PowerShell, preview and apply the
boot file update to `OUROSBOOT`. A Phase 23 volume gets a replacement
`OUROS.IMG` and `SHELL.ELF` and the new `WAVCHECK.OSH`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase24-serial-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the pendrive and boot the Pi. In PuTTY at `user:/$`, type
the first command, wait for its PASS line, and then type `exit`.

```text
source -e /boot/WAVCHECK.OSH
exit
```

At the `>` monitor prompt, run each command on its own.

```text
uartinfo
resinfo
storagetest
datacheck
```

Expect `PASS: all five WAV edits matched their expected hashes.` from
the script. Record the UART counters and compare the claims, USB hash,
and data bank generation with the preceding boot.

The Pi matched the tone and all five edit hashes and reaped all six
children. UART reported FIFO enabled and zero receive errors. The USB
readback hash and the mounted data generation matched Phase 23.
[Phase 24 Pi capture](../proofs/pi5-serial-batch-pass-2026-11-24.txt)

## Phase 25 Text pipelines

Five EL0 utilities extend file and pipe workflows. Each accepts one
optional file path. Omit the path or use `-` to read from stdin.
[Command syntax and limits](shell-workflows.md#text-pipeline-tools)

The QEMU suite checks repeatable runs, file and pipe input, binary
byte handling, line and count limits, and file-handle cleanup.
Preview, apply, readback, and rollback passed on copied boot files.

The boot update adds `TAIL.ELF`, `CUT.ELF`, `SORT.ELF`, `UNIQ.ELF`,
`TR.ELF`, and `TEXTCHK.OSH` to the Phase 24 boot volume. It uses the
Phase 24 kernel. In Windows PowerShell, from the project folder, use
the `OUROSBOOT` drive letter when previewing and applying the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase25-text-pipelines-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, run the script
and wait for its PASS line before entering `exit`.

```text
source -e /boot/TEXTCHK.OSH
exit
```

The script translates record separators to newlines, takes the final
two records, selects fruit names, and sorts and counts them. It checks
the output at each step by hash and removes its temporary files on
success. Expect `2 apple`, `1 banana`, and `1 berry`. If it stops after
creating `/tmp/textchk`, reboot to clear the temporary files before
retrying. At the kernel monitor, run `uartinfo`, `resinfo`,
`storagetest`, and `datacheck`.

The Pi matched all four hashes and printed the expected counts. All
five children were reaped, the pipeline transferred 25 bytes, and both
ends exited with code 0. UART reported zero errors, and resource and
storage checks matched the prior boot. `tail` is an EL0 application.
Type `appshell` at the kernel monitor to run it from `user:/$`.
[Phase 25 Pi capture](../proofs/pi5-text-pipelines-pass-2026-11-24.txt)

## Phase 26 Portable archives

`TAR.ELF` creates an uncompressed ustar archive from a directory,
lists its members, checks its structure, extracts a single file to
stdout, and compares every member against a directory tree.
[Command reference](shell-workflows.md#portable-archives)

Native tests covered nested paths, ustar prefix fields, truncated and
invalid headers, duplicate names, and the 32 KiB boundary. Windows
`tar.exe` extracted an archive made with the OurOS header writer and
supplied fixtures that OurOS read byte for byte. The
`test-shell-qemu.ps1 -ArchiveTools` suite passed on Cortex-A53 and
Cortex-A76, with zero open file handles after the tests.

The boot update adds `TAR.ELF` and `TARCHK.OSH` to the Phase 25
boot volume. In Windows PowerShell, from the project folder, use the
`OUROSBOOT` drive letter to preview and apply the update.

Preview, apply, readback, and rollback passed on copied Phase 25 boot
files. The Phase 26 ZIP has SHA-256
`e2d98cecee3fa881855ca75a2aeb34649c59eb1016c0e5ca6372cef74abc428d`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase26-archives-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the pendrive and boot the Pi. At `user:/$`, enter
`source -e /boot/TARCHK.OSH`. The batch builds a 4,096-byte archive
with a nested binary file and an empty directory, verifies its
structure, compares the extracted bytes and the source tree, and
removes its temporary files on success. Wait for
`PASS: archive creation, listing, validation, extraction, and comparison.`
Then type `exit` and run `uartinfo`, `resinfo`, `storagetest`,
and `datacheck` at the kernel monitor.

The Pi passed all five archive operations and reaped all seven
children. UART reported zero errors, and the USB storage hash and
`/data` generation matched the preceding boot. The standalone shell
uses the forms `tar list ARCHIVE` and `tar verify ARCHIVE`.
[Phase 26 Pi capture](../proofs/pi5-archives-pass-2026-11-24.txt)

## Phase 27 Kernel file handles and shell append

The kernel adds exclusive creation, append mode, handle metadata,
positioned reads and writes, and file resizing. The EL0 shell uses
the append mode for `>>` with a single program or a pipeline.
[File-handle ABI and semantics](file-handles.md#phase-27-el0-file-handles)

The host tests passed on v4 and v5, including mode, alias and owner
checks, cursor preservation, zero-filled growth, and allocator and disk
failure injection. Both Cortex-A53 and Cortex-A76 passed
`test-shell-qemu.ps1 -ExtendedFiles`, `-StreamSyntax`, and `-ArchiveTools`. They verified
that the boot partition matched the baseline, the appended v5 file
matched its hash after a fresh mount, and file handles were released.
The Pi header kernel has 4 KiB of space before the
firmware device tree. Preview, apply, readback, and rollback passed on
copied Phase 26 boot files. The update ZIP has SHA-256
`6171609eff9fc8a9a10c71fa7e50e454d942e2a45c8c6ced2fe5b81b735c8c3f`.

The Phase 27 update replaces `OUROS.IMG` and `SHELL.ELF` and adds
`FDCHK.ELF` and `FDCHK.OSH` on a Phase 26 boot volume.
In Windows PowerShell, from the project folder, use your
`OUROSBOOT` drive letter for the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase27-file-handles-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, run
`source -e /boot/FDCHK.OSH`. Expect the six file-handle checks to
pass, then `first`, `second`, and `third` on separate lines, followed
by `PASS: kernel file handles and shell append.` The batch writes its
test files under `/tmp` and removes them on success.

Type `exit`, then run `fileinfo`, `uartinfo`, `resinfo`,
`storagetest`, and `datacheck` at the kernel monitor. Expect
`Open files: 0/16` and the usual storage checks.

The Pi passed the extended file-handle checks and the shell append
hash. It reaped all five children, reported zero UART errors and
open handles, and preserved the USB and mounted-data baseline.
[Phase 27 Pi capture](../proofs/pi5-file-handles-pass-2026-11-24.txt)

## Phase 28 Descriptor lifecycle

The kernel adds duplicable file handles with shared cursors, access
reduction per token, runtime append control, per-token close-on-exec,
and a process-local handle inventory.
[Descriptor ABI](file-handles.md#phase-28-descriptor-lifecycle)

Native v4 and v5 checks covered slot exhaustion, owner isolation, shared
cursors, and release of the last lease. Both QEMU profiles passed the
descriptor batch, Phase 27 append and remount, and the existing exec
regression. The header
and debug kernels each leave 4 KiB before the firmware DTB. Update preview,
apply, readback, and rollback passed on a Phase 27 copy. The ZIP SHA-256
is `1354dbf674af8710c7ea40967974d9f9f7179e4dda3146222af00d8ed8423fb0`.

The Phase 28 update replaces `OUROS.IMG` and adds `FDDUP.ELF`
and `DUPCHK.OSH` on a Phase 27 boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase28-descriptors-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After safely ejecting the drive and booting the Pi, run
`source -e /boot/DUPCHK.OSH` at `user:/$`. Expect
`PASS: descriptor lifecycle and redirected exec.` The test replaces
its own image, checks that only the intended handle survives, and
writes its result to a temporary file through the retained stdout.
Type `exit`, then run `fileinfo`, `uartinfo`, `resinfo`,
`storagetest`, and `datacheck` at the monitor.

The Pi passed both descriptor checks with exit code 0, restored all
resources, and reported zero open handles and UART errors. The
`/boot/NONE.ELF` load failure is the intentional failed-exec case.
The repeated `datacheck` matched bank 0 at generation 1.
[Phase 28 Pi capture](../proofs/pi5-descriptors-pass-2026-11-24.txt)

## Phase 29 Process capacity and pipelines

- The kernel supports five active EL0 slots. The shell can use four
  for a pipeline, and `apprun` accepts up to five standalone programs.
  Ordinary job control retains its two-child limit.
- Each adjacent pair has a bounded FIFO. A stage blocks when the
  next pipe is full or its input pipe is empty. EOF propagates once
  the upstream writer closes and its buffered output is read. The
  scheduler runs one foreground pipeline at a time.
- Pipelines accept `<` at the first stage and `>` or `>>` at the
  last. The shell reports the rightmost nonzero stage status, or 0
  when every stage succeeds. Ctrl+C cancels the entire pipeline.
- The version 2 process snapshot holds five entries. The version 1
  snapshot remains usable by existing ELFs and includes the caller
  when the active batch exceeds its three-entry capacity.
- Process page tables use physical page allocator backing. A failed
  launch releases the stages prepared so far. A failed `exec` preserves
  the original process, and a successful `exec` installs its new root.

The Cortex-A53 and Cortex-A76 QEMU runs pass three- and four-stage
pipelines, end-to-end data checks, append, middle-stage `exec`, load
rollback, contained faults, cancellation, and resource cleanup. The
descriptor, process-control, stream syntax, exec, and memory-pressure
regressions also pass on both profiles. The header and debug builds each leave 72 KiB
before the firmware device tree. Preview, apply, readback, and rollback
pass against a Phase 28 boot-directory copy. The ZIP SHA-256 is
`d4713dee71a3ce2f978521321a4502fcf7fcb79dadaaa7734fab58bf7452f970`.

The Phase 29 boot update replaces `OUROS.IMG`, `SHELL.ELF`, and
`PS.ELF` and adds `PIPE29.OSH` on a Phase 28 volume. Use the Windows
PowerShell commands below from the project folder.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase29-pipelines-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive, boot the Pi, and type this in PuTTY at `user:/$`.

```text
source -e /boot/PIPE29.OSH
```

Look for `PASS: four-stage pipeline preserved all 16384 bytes.` Then
type `exit` and use these commands at the kernel monitor.

```text
fileinfo
uartinfo
resinfo
storagetest
datacheck
```

Expect a five-slot summary, restored app resources, zero open file
handles, and the same USB read hash and mounted data generation.
[Remaining core OS milestones](remaining-os-roadmap.md#core-os-milestones)

The Pi passed the four-stage pipeline at 16,384 bytes with 49,152 bytes
through the three rings, 26 reader waits and 35 writer waits. All four
children were reaped and the monitor reported zero open files.
[Phase 29 Pi capture](../proofs/pi5-pipe-chain-2026-11-24.txt)

## Phase 30 Core OS

- Direct parent and child processes can exchange messages of up to 96
  bytes. Each recipient holds four messages. A nonzero timeout parks
  the task on an empty receive or full send. A receive from a specific
  peer drains messages already sent, then returns `UOS_EPIPE` when the
  peer is gone.
- A message may duplicate one file handle with explicit read or write
  rights. The receiver shares the open description and its file offset.
  The kernel holds the copy until receive, and drops queued copies on
  exit or successful exec. Received handles are close-on-exec.
- A parent can send `SIGINT` or `SIGTERM` to stop a child, or `SIGUSR1`
  to wake a signal waiter or interrupt a blocked read, send, or sleep.
  An interrupted wait returns `UOS_EINTR` and leaves the signal pending
  for `uos_signal_wait`. The parent can inspect an unreaped child's
  stop record, including the exception PC, syndrome and valid fault
  address when applicable.
- Anonymous VM regions hold up to 32 zeroed pages each, with 48 mapped
  region pages per process and four active objects system-wide.
  The creator can grant read-only or read/write access to one direct
  child. All region mappings are non-executable. An attached child
  retains the backing until it unmaps or exits. Successful exec drops
  that process's mappings and unconsumed grants.
- The monitor supervises one EL0 service in a dedicated slot. The
  service can run while the monitor or shell is idle, and share the
  scheduler with a four-stage foreground pipeline. A nonzero exit
  triggers up to two restart attempts after a 250 ms delay. The monitor
  reports the last outcome. `svc stop` clears the service and releases
  its resources. Services use file I/O for output.

Both QEMU profiles pass the core check, exec mapping cleanup, a service
restart and heartbeat across shell sessions, and shell responsiveness
while a service runs a CPU loop. Native MMU, heap, and file tests pass.
The Pi header and debug builds each leave 52 KiB before the firmware
device tree. Preview, apply, readback, and rollback passed against a
Phase 29 boot-directory copy. The Phase 30 ZIP SHA-256 is
`1a888840650543aeed50f9e408d9b27a8fb34fd98671c7ed3d34152174373dbb`.

Use the Phase 30 package with the existing Pi header boot partition.
The update replaces `OUROS.IMG` and `PS.ELF` and adds `CORECHK.ELF`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase30-core-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

After safely ejecting the USB drive and booting the Pi, type `exit` in
the shell. At the monitor prompt, run the core application check.

```text
appterm 70 /boot/CORECHK.ELF
```

Expect the core IPC PASS line and restored app resources. The test's
null read and missing ELF are intentional fault and failed-exec cases.
Wait for the monitor prompt before entering the service commands.

```text
svc start /boot/CORECHK.ELF service
svc
cat /tmp/core30-beat
appshell
```

The sample service exits once with code 31. It restarts after 250 ms
and starts writing a heartbeat. If needed, repeat `svc` after a second
and check `active=yes launches=2 restarts=1`. From the shell, run the
pipeline check while the service remains active.

```text
source -e /boot/PIPE29.OSH
exit
```

At the monitor, confirm the service PID remains the same and the beat
has increased, then stop it and check cleanup.

```text
svc
cat /tmp/core30-beat
svc stop
fileinfo
rm /tmp/core30-started
rm /tmp/core30-beat
uartinfo
resinfo
storagetest
datacheck
```

Expect `active=no`, `cleanup=ok`, and `Open files: 0/16` after stopping.

The Pi passed the core application check with all seven children
reaped. The service restarted once, held PID 12 across the shell
reopen and passed the pipeline alongside it. Its heartbeat advanced
from 22 to 63. The service stopped with `cleanup=ok`, and the
monitor reported zero open files, no UART errors and the expected USB
hash and mounted data generation. [Phase 30 Pi capture](../proofs/pi5-core-os-2026-11-24.txt)
