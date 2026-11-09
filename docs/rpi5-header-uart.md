# Pi 5 CP2102 console on the 40-pin header

The `header` build uses RP1 UART0 on the large GPIO header. Standard female
Dupont jumper wires fit these pins. The earlier default image uses UART10 on
the small 3-pin connector and cannot provide its kernel console on these wires.
Both builds remain experimental. **The header-console kernel boot and monitor
have now been observed on the user's physical Pi 5 with a CP2102.** See the
[captured results and verification limits](pi5-hardware-verification.md).
The dedicated UART10 build has not been physically verified.

The first physical capture showed firmware loading the kernel but no OurOS
banner. It reported `RP1_UART 0000001c00030000` on board revision `d04171`.
The header candidate has been corrected to use that address. The earlier
header image used `0x1f00030000` and must be replaced for this firmware mapping.
The subsequent capture confirms the OurOS banner, EL2-to-EL1 transition,
working command input/output and advancing uptime with the corrected address.
The [RAM/page allocator revision](memory.md) also passed the physical Pi test.
The [RAMFS root and commands](ramfs.md) also passed the physical test. The new
[storage](storage.md) and [SDIO query](wifi.md) need their own Pi captures.
Older proof captures predate these revisions.

## Wiring

Disconnect Pi power and adapter USB before wiring. Remove any HAT that uses
these pins. These are **physical header pin numbers**, not GPIO numbers.

| Pi 5 physical pin | Signal | CP2102 connection |
| --- | --- | --- |
| 6 | Ground | GND |
| 8 | GPIO14 / Pi TX | RXD |
| 10 | GPIO15 / Pi RX | TXD |

Leave adapter VCC, 5V, 3V3 and handshake pins disconnected. Use 3.3 V UART
logic; a module's power-output jumper may not select its logic voltage.
Power the Pi separately through USB-C; connect adapter USB to the Windows PC.
Use the official [GPIO diagram](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)
to identify pin 1 before counting. Pins 6, 8 and 10 are adjacent on the even
numbered row. Do not confuse physical pins 14/15 with GPIO14/15.

## Image and Windows terminal

Build without WSL:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Whole-card artifact (128 MiB):

```text
build/rpi5-header/OurOS-Pi5-header-experimental-sd.img
```

Its checksum and JSON manifest are adjacent. The default debug-console image
remains in `build/rpi5/`. To verify or test the header image:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-rpi5-image.ps1 -Image build/rpi5-header/OurOS-Pi5-header-experimental-sd.img -Manifest build/rpi5-header/OurOS-Pi5-header-experimental-sd.img.json
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-rpi5-image.ps1 -Console header
```

1. Use Device Manager, Ports (COM & LPT), to identify the CP2102 COM port.
   If needed, install the official [CP210x VCP driver](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers).
2. Optional adapter-only loopback: disconnect it from the Pi, join TXD to RXD,
   open PuTTY with local echo off, and verify that typed letters return. Remove
   the jumper before connecting to the Pi.
3. In Raspberry Pi Imager select the header whole-card image using the custom
   image option. Confirm the intended card by identity before writing; the write
   replaces its contents. Skip Linux OS customisation. Allow write verification
   to finish, safely eject, then insert into the powered-off Pi.
4. Wire as above. Open PuTTY on the CP2102 COM port: 115200 baud, 8 data bits,
   no parity, 1 stop bit, no flow control. Set local echo and local line editing
   to Force off. Enable All session output logging to a new log file.
5. Open the terminal before powering the Pi. Look for the OurOS banner:

```text
OurOS: AArch64 kernel booted
Firmware configured PL011 at 0x0000001c00030000 (baud/clock preserved)
Board=rpi5-header entered EL2 running EL1
Kernel monitor ready. Type help.
>
```

The reported entry EL can differ; save the actual value. Run `help`, `info`,
`cpuinfo`, `echo pi5-ok`, and `uptime`. Repeat `uptime` after ten seconds and
check that it advances. Repeat the boot to check consistency. In the new memory
revision, also run `meminfo`, `memmap` and `pagetest`; see the memory guide.
HDMI is blank, and USB keyboards/networking
are unsupported. Capture the complete log, image hash and EEPROM version if
known. These results establish a kernel boot milestone, not a complete OS.

No output: check COM port, ground, correct physical pins, crossed TX/RX,
3.3 V signals, adapter loopback and that the header image was flashed.
Firmware logs without the OurOS banner show that at least the transmit path
works, but do not establish kernel execution. Send the full log for debugging.
Do not reflash the EEPROM simply because output is missing.

## Implementation and limits

The separate config uses `enable_rp1_uart=1` and `pciex4_reset=0`.
Raspberry Pi documents the first as initialising RP1 UART0 to 115200 for
bare-metal debugging, and the second as preserving PCIe configuration across
OS handoff. See the [firmware configuration reference](https://www.raspberrypi.com/documentation/computers/config_txt.html#enable_rp1_uart).
There is no Linux `cmdline.txt` or serial-getty dependency.

The compile-time `OUROS_RPI5_HEADER_UART` selection changes the board name and
UART base to `0x1c00030000`, taken from the actual firmware `RP1_UART` capture.
The former `0x1f00030000` assumption came from a Linux device-tree PCIe window;
that window does not establish firmware's selected RP1 BAR/window before a
PCIe driver reconfigures it. The RP1 UART0 peripheral offset is `0x30000`.
References for the peripheral and the available mappings:

- [RP1 UART0 node](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/rp1.dtsi)
- [Pi 5 RP1 mapping](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts)
- [BCM2712 PCIe2 ranges](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)
- [RP1 peripheral manual, section 3.2](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)

The existing polling PL011 driver preserves firmware clocks/divisors, requires
UART and TX enabled, enables RX and masks UART interrupts. It does not discover
PCIe BARs or reinitialise the PCIe link, RP1 clocks or pinmux. It assumes the
documented firmware setup and this fixed mapping. Older/incompatible EEPROM
firmware or a changed mapping can cause an early silent halt or MMIO fault.
QEMU virt does not emulate this Pi 5 path. The linked physical capture verifies
the current header path on the tested board/setup; other mappings remain untested.

The captured `cmdline.txt not found` message is not a missing OurOS command
line requirement: this kernel does not consume one. A custom
`armstub8-2712.bin` is also not supplied; the documented default Arm stub is
embedded in firmware. Neither message by itself establishes the reason for
the absent banner. The missing `bcm2712d0.dtbo` file is a separate incomplete
DTB packaging issue on this board; its upstream overlay adjusts GPIO/pinctrl,
UART interrupt and DMA descriptions. Our polling console does not consume
those descriptions, and adding that overlay does not correct the erroneous
hard-coded UART address. Full D0 device-tree support remains outstanding.

After installing the rebuilt image, capture from power-on through the last
message and try `help` followed by Enter even if there is no banner. If no
prompt appears, save the complete log, including the EEPROM version line.
The subsequent captured monitor session verifies startup and reception for
this setup. Broader board/firmware compatibility and repeatability checks remain.

Build/package/image checks distinguish the console profiles and compare the
selected config, kernel marker and extracted file hashes. Negative tests
reject corruption and a mismatched console manifest. No physical card is
written by any build or verification script.

The user boots this header-console image from a 32 GB USB pendrive, without a
microSD card. The `-sd.img` suffix is a retained disk-image filename, not a
restriction to SD media. [USB commands](usb.md) reuse the working firmware RP1
window without resetting PCIe or the console. USB2 enumeration is physically
observed. The new `storageinit` activates read-only USB BOT when SD is absent or
unclocked, then attempts FAT32 at `/boot`; that complete path passes QEMU tests
and now has Pi sector/file-read evidence. Run it before `usbtest`/`usbenum` on a fresh
boot, since the commands share a one-shot controller session.
