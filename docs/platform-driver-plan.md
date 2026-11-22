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
