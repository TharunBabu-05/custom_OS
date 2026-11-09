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
SDIO/chip identification has header-console physical evidence.
USB block/FAT reads and ring wrap have header-console Pi evidence.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
RAM files disappear after reboot. storageinit attempts a read-only SD/FAT32 mount
at /boot using a firmware-selected SDHC/SDXC card; no full card init or writes.
Firmware can load this disk image from USB. Experimental kernel USB2 BOT reads
require the separate header-console build's known RP1 mapping.
usbinfo inventories DT hosts. usbprobe requires the separate header-console
build's known RP1 window; register probing is disabled with this UART10 build.
See docs/usb.md. On the header build use storageinit before usbtest/usbenum
on a fresh boot; USB capacity/sectors/FAT32 now have physical Pi evidence.
usbenum device/configuration/BOT descriptor reads now have Pi evidence.
usbtest is available with the header-console profile's known RP1 mapping only;
it takes over a connected host for four No-op completions and then halts it.
wifiinfo and wifiprobe provide DT/SDIO diagnostics only, not scan/connect/IP.
wifiinit adds guarded SDIO device selection and CCCR/CIS/CMD53 reads on an
legacy host, with guarded C0/C1/D0 WLAN power/pin/clock preparation if cold.
Cold start, SDIO identity and function-1 chip 0x4345 revision 6 have physical
evidence on the header build. WPA3 remains unfinished.
wificores core discovery and 800 KiB RAM sizing now have header-console evidence.
Complete disk images include pinned Cypress radio assets and WIFILIC.TXT;
the boot-files-only ZIP does not. wififiles checks their SHA256 pins.
wifiload requires storageinit/wifiinit/wificores and the exact observed profile.
It keeps the radio CPU halted, resets D11 and uploads/verifies code and NVRAM.
Full upload/readback now has physical header-console Pi evidence.
wifistart explicitly releases CR4 after verified staging and checks HT/shared
memory. Physical header-console startup now passes. wificontrol enables F2
and requests ioctl/firmware version replies; physical control now passes.
wificaps reads features and MAC; wifiprepare IN loads pinned CLM and verifies
the country. Physical preparation now passes for IN revision 0.
wifiscan adds passive discovery and bounded network/security result parsing.
Physical scanning now passes including an SAE/required-PMF hotspot.
cryptotest/rngtest add HMAC/HKDF tests and manual RNG200 source checks.
Native tests pass; physical RNG remains UNTESTED. No authentication or IP.
See docs/wifi-auth-foundation.md. No password is embedded in this image.
The transfer correction sets F1 block size=64 and uses <=64-byte RAM requests,
with explicit host block counting and retained first-failure diagnostics.
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
