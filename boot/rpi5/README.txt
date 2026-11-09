OurOS Pi 5 UART bring-up candidate - HARDWARE UNTESTED

This folder is a boot-file bundle, not a partitioned SD-card image.
ouros-rpi5.img is a raw AArch64 kernel Image, NOT a disk image for Pi Imager.
Do not flash that file as a whole disk.

The intended target is Raspberry Pi 5 Model B with its existing EEPROM firmware
and default embedded Arm stub. No Linux kernel, custom armstub, start.elf,
bootcode.bin, or EEPROM update/recovery binary is included.

Requires the dedicated 3-pin UART connector between the HDMI ports, a suitable
3.3 V USB UART adapter/cable, and terminal settings 115200 8N1, no flow control.
This port does not use the 40-pin header UART. No HDMI output is implemented.

Expected first kernel message: OurOS: AArch64 kernel booted
Type info to record the entry EL and current EL. New memory commands: meminfo,
memmap, pagealloc, pagefree <slot>, pagetest. DTB memory/reservation discovery and
4 KiB physical pages have evidence on the separate header-console Pi build.
MB/MiB displays and RAMFS commands also have physical header-console evidence.
New storage/SDIO paths are host/QEMU-tested; their physical tests are pending.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
RAM files disappear after reboot. storageinit attempts a read-only SD/FAT32 mount
at /boot using a firmware-selected SDHC/SDXC card; no full card init or writes.
Firmware can load this disk image from USB, but kernel USB disk I/O is pending.
usbinfo inventories DT hosts. usbprobe requires the separate header-console
build's known RP1 window; register probing is disabled with this UART10 build.
See docs/usb.md. No USB enumeration or pendrive mounting is implemented yet.
usbtest is available with the header-console profile's known RP1 mapping only;
it takes over usb0 for four No-op command completions and then halts it.
wifiinfo and wifiprobe provide DT/SDIO diagnostics only, not scan/connect/IP.
wifiinit adds guarded SDIO device selection and CCCR/CIS/CMD53 reads on an
already powered/clocked legacy host. WPA3-Personal connection remains unfinished.
Dynamic reservation windows are conservatively excluded in full.

Consult docs/rpi5-boot.md in the source repository before deployment. Creating
or writing a physical boot partition requires separate identification of the
destination device, backup planning, and the owner's explicit confirmation.
Do not overwrite an existing config.txt or reformat a card blindly.

The DTB and license files come unchanged from raspberrypi/firmware at the
revision recorded in manifest.json. COPYING.linux covers GPL-licensed Linux
material, including the supplied board description. LICENCE.broadcom is retained
from the upstream boot assets. No Broadcom firmware blob is bundled here.
Retain notices; source provenance is recorded in the manifest. Before public
distribution, establish corresponding source delivery for the exact DTB and
choose the license for the original OurOS code. This is a local test bundle.

manifest.json and SHA256SUMS record this package's contents. They detect
accidental corruption; they do not establish hardware boot compatibility.
