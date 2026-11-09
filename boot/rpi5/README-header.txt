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
MB/MiB displays and RAMFS also have physical Pi evidence.
SDIO selection and chip 0x4345 revision 6 are physically observed.
USB capacity/sectors/FAT files now pass on the physical Pi, including ring wrap.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
The root filesystem is volatile; changes disappear after reboot. See docs/ramfs.md.
Dynamic reservation windows are conservatively excluded in full.
storageinit: SD firmware handoff or USB2 BOT when SD is absent/unclocked,
then read-only FAT32 at /boot. storageinfo: selected backend and mount state.
The user's hardware boots from a 32 GB USB pendrive with no microSD card.
On a fresh boot run storageinit BEFORE usbtest/usbenum. It includes enumeration.
Then ls /boot, cat /boot/CONFIG.TXT, storagetest, storageinfo, cat again.
The captured SanDisk Cruzer Blade reports 30,765,219,840 bytes and mounts FAT32.
usbinfo: DT USB host inventory. usbprobe: read-only xHCI controller/port snapshot.
usbenum reads descriptors of a directly attached USB2 device after a DMA check.
VID 0781/PID 5567 and BOT endpoints 81/02 are physically observed on usb1.
storageinit selects configuration/bulk endpoints and reads SCSI LUN 0.
Only direct USB2 BOT disks with 512-byte sectors; no hubs/USB3/UAS/hotplug.
Controller discovery is now physically observed. usbtest selects a connected host,
tests four No-op DMA completions, and halts it. Reboot before repeating.
Does not reset PCIe/UART, issue USB transfers, or read/write the pendrive.
wifiinfo: actual DT inventory; wifiprobe: optional SDIO CMD5 query only.
wifi is an alias of wifiinfo. wifiinit: guarded SDIO selection/CCCR/CIS reads
with guarded C0/C1/D0 WLAN power, pin and 400 kHz setup for a cold host.
WLAN cold preparation and CMD52 SDIO identity now have physical evidence.
Function-1/ALP/ChipCommon identification now passes on the physical Pi.
wificores: five cores and 800 KiB TCM are now physically observed; selector restored.
The complete disk image includes pinned vendor radio assets and WIFILIC.TXT.
The boot-files-only ZIP does not include those radio assets.
wififiles: SHA256 verification of the three radio input files, without radio I/O.
wifiload: after storageinit, wifiinit, wificores; require the observed profile,
keep CR4 halted, reset D11, upload code/NVRAM and compare every byte read back.
Full upload/readback now passes on the physical Pi: 609309 firmware bytes and
1748 packed NVRAM bytes. It may take several minutes at 400 kHz SDIO.
F1=64 bytes, host block counting and <=64-byte RAM requests now pass on the Pi.
First SDIO failure diagnostics preserve command/R5/IRQ/FIFO stage and byte count.
wifistart: after verified wifiload, install vector, release CR4, check HT/shared
memory. Physical startup now passes: CSR 0xd0, shared 0x201cc0, flags 1.
wificontrol: after wifistart, enable F2, check protocol/credits, query ioctl ABI
and firmware version. Physical control now passes: ioctl ABI 2, firmware 7.45.265.
wificaps: read actual features/MAC, PMF and firmware supplicant properties.
wifiprepare IN: verify pinned CLM, upload it, check status/version, set/read IN.
Physical capability/preparation now passes: sae_ext, PMF, CLM status 0, IN rev 0.
sup_wpa reports unsupported; this is a capability result, not a transport fault.
wifiscan: after preparation, enable events/interface and request passive scan.
Print bounded SSID/BSSID/channel/RSSI/security results, then DOWN/restore mask.
Physical scanning now passes, including Jack's / channel 6 / SAE / required PMF.
cryptotest: HMAC/HKDF published known-answer tests.
rngtest: DT-gated RNG200 setup and two conditioned samples, erased/not printed.
Native RNG/crypto tests pass; physical RNG remains UNTESTED. No authentication/IP.
Capture wifiscan, wifiinfo and final tests. Reboot before a fresh scan/retry.
See docs/wifi-auth-foundation.md. No password is embedded or accepted by this build.
The unsupported function-0 CMD53 CIS probe was removed.
WPA3-Personal is the requested target. No Wi-Fi association or IP connection.
No Wi-Fi scan/connect/IP, HDMI/GUI, USB keyboard, heap, MMU or processes.
See docs/storage.md and docs/wifi.md.
See docs/rpi5-header-uart.md. No Linux kernel or EEPROM update is included.

DTB and notices come unchanged from raspberrypi/firmware at the manifest
revision. Retain COPYING.linux and LICENCE.broadcom; corresponding-source
delivery and original-code licensing remain pending before public distribution.
Manifest hashes verify file integrity, not hardware boot compatibility.
