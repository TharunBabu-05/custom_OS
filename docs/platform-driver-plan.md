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
General resource claims and conflict tracking follow this inventory.
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
`phase13-ok`.
