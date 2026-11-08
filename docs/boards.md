# Board identity and boot integration plan

**Current Pi 5 implementation:** see [rpi5-boot.md](rpi5-boot.md). A compiled
candidate and checked boot-file ZIP exist. Its firmware-entry assembly passes
EL1/EL2 QEMU tests. The separate RP1 header-console build has now booted on the
user's Pi 5 with a CP2102: [captured hardware results](pi5-hardware-verification.md).
UART10 and the remaining acceptance criteria still need their own verification.

## Identity

The user confirmed **Raspberry Pi Zero 2 W**, not the original Zero or Zero W.
The Zero 2 W's RP3A0 package contains BCM2710A1 and 512 MB RAM. Its Cortex-A53
supports AArch64. The original Zero's ARM1176JZF-S cannot execute AArch64 code.
This resolves the original document's requested model check; an ARMv6 build is
not needed for the confirmed hardware.
[Raspberry Pi Zero documentation](https://www.raspberrypi.com/documentation/computers/zero.html)
and [processor documentation](https://www.raspberrypi.com/documentation/computers/processors.html).

Before physical deployment, record each board's printed model/revision and the
Pi 5 RAM capacity. If an existing OS is already running, read-only commands are:

```bash
tr -d '\0' < /proc/device-tree/model
printf '\n'
cat /proc/cpuinfo
```

These commands run on an existing Linux installation on the Pi, not on Windows
or our future OS. No Linux installation is required to develop the new kernel.
Use the board's revision information/packaging for physical capacity; do not
equate Linux's free RAM with installed RAM. The custom kernel will discover
usable memory through the firmware DTB and reserved ranges.

## Separate boot paths

| Item | Pi Zero 2 W | Pi 5 |
| --- | --- | --- |
| Vendor bootstrap | ROM and SD firmware including `bootcode.bin` | ROM and SPI EEPROM bootloader |
| CPU mode | Explicit AArch64 selection in firmware configuration | 64-bit kernel boot |
| Kernel filename policy | Explicit `kernel=` to avoid implicit selection | Explicit `kernel=` to avoid implicit selection |
| Firmware package | Pin a compatible Zero 2 W firmware/DTB set | Pin Pi 5-compatible assets and record EEPROM version |
| UART work | Verify UART instance, pin mux, clock and Bluetooth interaction | Select and document debug-UART route; account for BCM2712/RP1 separation |

Raspberry Pi documents the [EEPROM/SD boot distinction](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#raspberry-pi-boot-eeprom).
The firmware's kernel filename defaults differ by board; Pi 5 normally prefers
`kernel_2712.img` and can fall back to `kernel8.img`. A custom kernel will set an
explicit filename and load contract. `arm_64bit=1` selects 64-bit mode on Zero 2 W;
Pi 5 only supports the 64-bit kernel path.
[Official config.txt reference](https://www.raspberrypi.com/documentation/computers/config_txt.html#kernel).

The initial choice is vendor firmware loading our own kernel directly, subject
to Phase 2 boot-protocol verification. U-Boot is optional; GRUB/Limine are not
assumed to work on these boards. Vendor bootstrap firmware initializes hardware;
it does not replace our kernel with Linux.

## Phase 2 acceptance criteria

For each board's validated deployment:

1. Pin firmware source revision and retain its license notices/checksums.
2. Verify exact required files for that board, including DTB and any overlays.
3. Verify firmware load address, image-header requirements, DTB handoff, entry EL,
   core startup state, MMU/cache state and RAM reservations.
4. Resolve the UART route using official board schematics, device tree and
   peripheral documentation; record address, access width, clock and pin mapping.
5. Implement architecture entry and a board-specific UART driver, then test them.
6. Generate a regular disk image with an appropriate FAT boot partition, explicit
   board configuration, verified firmware assets, DTB and our kernel. Check the
   partition table/FAT contents and manifest before proposing any physical write.

The Pi 5 candidate now covers build-time checks, boot-file packaging and a
[partitioned experimental SD image](disk-image.md). A shared [memory-only DTB
reader and physical page allocator](memory.md) pass host/QEMU tests and have a
physical Pi memory capture. The [RAMFS root](ramfs.md) also has a physical capture.
The latest [storage](storage.md) and [Wi-Fi diagnostic](wifi.md) paths need new
Pi testing; the QEMU block/FAT32 path is tested against a read-only image.
Full Pi device-tree support and broader firmware
compatibility remain unfinished.
Firmware entry and bidirectional header UART operation were observed on the
tested Pi 5 setup, as recorded in the hardware results.
The Zero 2 W boot package is not implemented.

Use a 3.3 V TTL serial adapter and the documented connector/pinout selected for
the specific board; verify wiring before powering it. Do not substitute RS-232
voltage levels. Pi 5 debug UART access must not be inferred from a Zero pinout.

## Emulation and debugging

The implemented generic emulator board is documented in [first-kernel.md](first-kernel.md).
Native Windows run/test/debug commands are in [windows.md](windows.md).

[QEMU's documented Raspberry Pi models](https://www.qemu.org/docs/master/system/arm/raspi.html)
include neither Pi 5 nor Zero 2 W. Check `qemu-system-aarch64 -machine help` for
the installed version. `raspi0` is an original ARM32 Zero model, not a Zero 2 W.

Use a separate [QEMU `virt` board](https://www.qemu.org/docs/master/system/arm/virt.html)
to validate architecture/common kernel code. A Cortex-A53 or Cortex-A76 CPU model
does not reproduce the corresponding Pi's peripherals. On an x86 Windows laptop,
use QEMU TCG emulation; do not expect ARM KVM acceleration.

The `qemu_virt` kernel now exists. `scripts/run-qemu.ps1 -Gdb` starts it paused
with a GDB listener bound to localhost. Native `aarch64-none-elf-gdb.exe` has been
tested reaching `kernel_main` and checking initial state. Real boards initially
use UART logs; JTAG/OpenOCD requires separately verified probe/target configuration.
