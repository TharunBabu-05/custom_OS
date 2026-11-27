OurOS Pi 5 40-pin header UART profile - verified on Raspberry Pi 5

This bundle contains individual boot files. Flash a whole-disk image if
you need both the FAT32 boot partition and the persistent data partition.
ouros-rpi5.img is the raw AArch64 kernel Image for the boot partition.

Header UART uses RP1 UART0 at 115200 baud, 8N1, with no flow control.
Pi physical pin 8 (GPIO14/TX) -> adapter RXD
Pi physical pin 10 (GPIO15/RX) <- adapter TXD
Pi physical pin 6 (GND)      -- adapter GND
Use 3.3 V UART signals and leave the adapter power pins disconnected.

The profile inherits RP1 UART clock, pinmux and PCIe configuration from
firmware. It selects CPU physical address 0x1c00030000, observed on board
revision d04171. The kernel loads at 0x80000. This config places the DTB
at 0x02000000. For older firmware layouts, the entry code can copy a
valid DTB to that location before initializing BSS.

The Pi 5 USB boot path mounts FAT32 at /boot and supports v5 persistent
storage at /data. Complete images use ouros.startup=shell to launch the
EL0 shell. At the monitor, startup performs the same boot sequence.
If an HDMI framebuffer is available, startup enables its text mirror
and scans the active USB storage controller for a direct boot keyboard.

Useful monitor commands include info, memmap, usbports, usbkbd,
fbinfo, smpstart, smpirq on, appshell, storagetest and datacheck.
Historical Pi captures and their scope are indexed in proofs/README.md
in the source repository. Each new build has its own acceptance status.

The pinned DTB and license notices come from raspberrypi/firmware.
Preserve COPYING.linux and LICENCE.broadcom and provide corresponding
source as applicable when distributing the DTB. Original OurOS source
uses the MIT License. See PROVENANCE.md in the source repository.
