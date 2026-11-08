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
Type info to record the entry EL and current EL. meminfo intentionally reports
RAM discovery unavailable on this board; no Pi RAM allocator is implemented.

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
