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
