OurOS Pi 5 40-pin header UART candidate - HARDWARE UNTESTED

This is a boot-file bundle, not a partitioned SD-card image.
ouros-rpi5.img is a raw kernel; do not flash it as a whole card.

Console: RP1 UART0, 115200 8N1, no flow control.
Pi physical pin 8 (GPIO14/TX) -> CP2102 RXD
Pi physical pin 10 (GPIO15/RX) <- CP2102 TXD
Pi physical pin 6 (GND)      -- CP2102 GND
Use 3.3 V UART signals. Leave adapter VCC/5V/3V3 disconnected.
Power the Pi through its USB-C supply. Wire with power disconnected.

Requires firmware supporting enable_rp1_uart=1 and pciex4_reset=0.
Inherits firmware pinmux, UART clock/divisors and PCIe mapping. No general
PCIe driver or RP1 reset/reinitialization is implemented. Physical operation
and compatibility with the installed EEPROM still require a captured boot log.
This kernel selects RP1 UART0 at CPU physical address 0x1c00030000, matching
the captured RP1_UART firmware message on board revision d04171. This is a
firmware-mapping-specific candidate; do not assume every EEPROM uses it.
The dedicated 3-pin UART connector is not this build's kernel console.

Expected: OurOS: AArch64 kernel booted; Board=rpi5-header; running EL1.
Type help, info, cpuinfo, echo pi5-ok, and uptime (twice, 10 seconds apart).
New memory commands: meminfo, memmap, pagealloc, pagefree <slot>, pagetest.
Memory discovery/reservations and 4 KiB pages now have physical Pi evidence.
New MB/MiB displays and RAMFS are host/QEMU-tested; their physical test is pending.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
The root filesystem is volatile; changes disappear after reboot. See docs/ramfs.md.
Dynamic reservation windows are conservatively excluded in full.
No HDMI/GUI, USB keyboard, networking, SD filesystem driver, heap, MMU or processes.
See docs/rpi5-header-uart.md. No Linux kernel or EEPROM update is included.

DTB and notices come unchanged from raspberrypi/firmware at the manifest
revision. Retain COPYING.linux and LICENCE.broadcom; corresponding-source
delivery and original-code licensing remain pending before public distribution.
Manifest hashes verify file integrity, not hardware boot compatibility.
