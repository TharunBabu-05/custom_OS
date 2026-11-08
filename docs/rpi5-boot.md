# Pi 5 UART boot candidate

**40-pin header alternative:** a separate RP1 UART0 build now supports testing
with a CP2102 and standard jumper wires. See [rpi5-header-uart.md](rpi5-header-uart.md).
The default UART10 build described below still uses the small 3-pin connector.

A separate whole-card experimental image is now available via
`scripts/image-rpi5.ps1`; see [disk-image.md](disk-image.md). The kernel `.img`
and boot-file ZIP described below remain distinct artifacts and must not be
mistaken for that whole-card image.

The Pi 5 build and local boot-file package are ready for hardware bring-up.
**The header-console kernel has booted on the user's physical Pi 5 with a
CP2102**, including working commands and advancing uptime. See the
[hardware evidence](pi5-hardware-verification.md). The default UART10 build
described here still needs its own physical test and matching connector cable.
The new [physical-memory subsystem](memory.md) adds RAM/reservation discovery
and page allocation, tested on the host/QEMU; its Pi test is pending. No HDMI,
USB keyboard, storage driver, heap or MMU is implemented.

## Build and package on Windows

In **Windows PowerShell**, at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target rpi5
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-rpi5.ps1
```

The package command rebuilds the kernel, downloads missing assets from the
pinned upstream revision, verifies their hashes, and creates:

```text
build/rpi5/kernel.elf                  Symbols for debugging/disassembly
build/rpi5/ouros-rpi5.img               Raw AArch64 kernel Image
build/rpi5/ouros-rpi5-boot-files.zip    Boot files with config/DTB/licenses/hashes
```

**The kernel `.img` is not a disk image. Do not feed it to Raspberry Pi Imager.**
The ZIP is a set of files intended for the root of a dedicated FAT boot partition,
not a partitioned image either. Its extracted contents are:

| File | Purpose |
| --- | --- |
| `ouros-rpi5.img` | Our original kernel, with 64-byte AArch64 Image header |
| `config.txt` | Explicit Pi 5 load addresses and debug-UART setup |
| `bcm2712-rpi-5-b.dtb` | Official board description for firmware to patch |
| `COPYING.linux`, `LICENCE.broadcom` | Retained upstream license notices |
| `README.txt` | Bundle scope, hardware requirements and deployment limits |
| `manifest.json`, `SHA256SUMS` | File lengths, hashes and upstream revision |

Packaging writes only under `.dist/` and `build/`. Every run stages into a new
directory, so stale boot/recovery files cannot enter the package. The script
verifies the staged files and then extracts/verifies the completed ZIP. It has
no physical disk or drive-letter parameter.

The official assets are pinned in `boot/rpi5/assets.json` to
[`raspberrypi/firmware` revision ab5c7f8](https://github.com/raspberrypi/firmware/tree/ab5c7f86283ceb58b7adb269cae1d16092a31bf6/boot).
The installed EEPROM revision is unknown and must be recorded during bring-up;
pinning a DTB does not prove compatibility with every EEPROM revision. No vendor
firmware binaries or Linux kernel are bundled. The DTB comes from Linux-derived
sources; retain its license and establish exact corresponding-source delivery
before distributing the bundle publicly. Original OurOS licensing remains undecided.

## Entry contract and memory layout

Pi 5's EEPROM contains the startup firmware and default Arm stub. We keep those
components and load our own kernel. Pi 5 does not require the earlier models'
`start.elf`/`fixup.dat` boot files. The TF-A Pi 5 platform documents EL2 payload
entry, a configurable kernel address, and DTB handoff in x0 through its shared
setup code. See [TF-A Pi 5 boot design](https://github.com/TrustedFirmware-A/trusted-firmware-a/blob/main/docs/plat/rpi5.rst),
[handoff source](https://github.com/TrustedFirmware-A/trusted-firmware-a/blob/main/plat/rpi/common/rpi4_bl31_setup.c),
and [Raspberry Pi boot configuration](https://www.raspberrypi.com/documentation/computers/config_txt.html).

| Region | Candidate layout |
| --- | --- |
| Below `0x80000` | Left untouched for firmware/secure monitor |
| `0x80000` | Kernel Image and entry point, explicitly set in config.txt |
| After code/data | BSS and a 64 KiB, 16-byte-aligned kernel stack |
| `0x200000` through `0x2fffff` | Explicit DTB allocation window |

The linker asserts that code, data, BSS and stack end before the DTB window.
The image header advertises the full in-memory footprint, including BSS/stack.
The new memory reader discovers root RAM banks and reservations independently
of the QEMU UART parser. It protects kernel/BSS/stack/bitmap, DTB, boot guard and
firmware regions before offering 4 KiB pages below 32 GiB. See [memory.md](memory.md)
for conservative dynamic-window handling and the pending real-Pi test.

`arch/aarch64/boot/firmware.S` preserves x0, masks exceptions, parks unexpected
secondary cores, and accepts EL1 or EL2. It rejects entry with the MMU or data
cache enabled. From EL2 it selects AArch64 EL1, disables stage-2 translation,
allows physical timer/counter access, initializes EL1 control state and returns
to EL1h with DAIF masked. It then establishes the stack, clears BSS, installs
fatal vectors and calls the shared kernel with the DTB pointer and entry EL.
Unsupported entry state parks silently before console setup.

The 64-byte Image format is the
[documented AArch64 boot ABI](https://docs.kernel.org/arch/arm64/booting.html).
Using that firmware interface does not link or run Linux. Register initialization
constants are cross-checked against
[TF-A's architectural register definitions](https://github.com/TrustedFirmware-A/trusted-firmware-a/blob/main/include/arch/aarch64/arch.h).
The assumptions about actual EEPROM handoff still need measurement on the board.

## UART10 and cabling

The selected console is the **dedicated 3-pin connector marked UART between the
HDMI ports**. The Pi 5's 40-pin header UART belongs to a different peripheral
path and is not supported by this initial port.
[Official UART configuration](https://www.raspberrypi.com/documentation/computers/configuration.html#configuring-uarts).

`boards/rpi5/board.c` selects CPU physical address `0x107d001000`. Raspberry Pi's
BCM2712 description maps the SoC bus range into the `0x10...` physical region and
defines the PL011 at bus address `0x7d001000`; the Pi 5 board aliases that UART
as UART10. TF-A independently uses the same physical base.
[BCM2712 device tree](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi),
[Pi 5 board description](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts),
[TF-A Pi 5 register map](https://github.com/TrustedFirmware-A/trusted-firmware-a/blob/main/plat/rpi/rpi5/include/rpi_hw.h).

The new `uart_attach_firmware` path checks that UART and TX are enabled, preserves
baud divisors, clocks, pin setup and framing, masks UART IRQs, and enables RX.
It depends on firmware initialization requested by `uart_2ndstage=1` and
`init_uart_baud=115200`. If UART/TX is disabled, initialization fails and the
kernel parks; it does not guess clock or pinmux registers. Actual firmware
setup and electrical I/O are **hardware untested**.

Use a Raspberry Pi Debug Probe's **U/UART port** and matching 3-pin cable, or
a 3.3 V TTL USB UART adapter with a correctly wired JST-SH cable. The probe's
SWD port is not the UART port. According to the
[official connector specification](https://datasheets.raspberrypi.com/debug/debug-connector-specification.pdf):

| Pi connector pin | Target signal | Generic adapter connection |
| --- | --- | --- |
| 1 | Pi RX | Adapter TX |
| 2 | Ground | Adapter ground |
| 3 | Pi TX | Adapter RX |

Use the keyed connector drawing to identify pin numbers; don't infer physical
left/right from this table. Do not connect adapter power/VCC or RS-232-level
signals. Power the Pi normally through its USB-C supply. Terminal configuration
is **115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**.
The [Debug Probe documentation](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
describes its USB UART interface and cables.

## Hardware test procedure (pending)

1. Obtain the adapter/cable and record Pi 5 model, RAM variant and EEPROM version
   if available. Keep the existing working card untouched while preparing a test.
2. Identify the intended microSD device and what is on it. Explain and confirm
   the exact formatting/partitioning or file-overwrite operation before making
   any physical write. No such operation has been performed or authorized yet.
3. After that separate confirmation, prepare a dedicated FAT boot partition and
   place the reviewed bundle files at its root. Use the bundled config, without
   adding an old custom armstub, initramfs or recovery/EEPROM-update image.
4. With the Pi powered off, connect the UART adapter and open its Windows COM
   port in PuTTY at the settings above. Enable session logging. Power on the Pi.
5. Capture firmware messages and the `OurOS: AArch64 kernel booted` banner. Run
   `info`, `cpuinfo`, `uptime`, and `echo pi5-ok`; record both entry and running EL.
6. Save that capture and exact package hashes. Only then mark Pi UART boot as
   hardware verified. HDMI remaining blank is expected for this kernel.

No output: first check connector, COM port, ground and settings. Firmware logs
without our banner: check file-load errors, image/load address and entry-state
assumptions; unsupported EL/MMU/cache state or an uninitialized UART can currently
park silently. Garbled output: check baud/framing and firmware UART setup.
Do not reflash EEPROM as a first troubleshooting step.

## What is tested now

The normal QEMU monitor suite, native GDB startup check and bounded DTB host tests
still pass after the shared kernel interface changes. The Pi 5 candidate builds
with Cortex-A76 flags and passes ELF/linkage/Image-header checks.

To test the **same firmware-entry assembly** without Pi hardware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1 -Handoff
```

This test links the entry code at `0x40080000`, uses QEMU's board implementation,
and boots the raw Image through QEMU's firmware-style handoff. Both EL1 and EL2
entry cases pass monitor/timer/input tests; a deliberate exception after EL2
entry also passes. The log reports `entered EL2 running EL1`. This verifies the
architectural transition, not the Pi image's physical load or its UART10 attach
path. Logs are under `build/handoff-test/`.

While the hardware test awaits an adapter, the next independent kernel work can
be timer interrupts and interrupt-controller support on QEMU, followed by memory
management. The physical boot acceptance criterion remains outstanding.
