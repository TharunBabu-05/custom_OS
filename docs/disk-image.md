# Experimental Pi 5 disk image

Current OS test: [exceptions/GICv2/timer IRQs](interrupts.md), opt-in after
`mmuinit` with `irqinit` and `timerstart`. The latest Pi run passes MMU
protection and USB/FAT reads. New IRQ delivery and driver regression need a
fresh hardware run; earlier captures verify DNS/Internet ping and cleanup.
The full [OS/driver checklist](os-status.md) records what is still missing.

Builds now preserve previous and newly generated images in the local
[`image_files` folder](image-archive.md), then clean known stale build outputs.
The newest complete header disk image is
`image_files/OurOS-Pi5-header-experimental-sd.img`; older versions have readable
phase or date/time filenames directly in that same folder.
Each completed build also generates `OurOS-Pi5-header-experimental-sd.zip`
(or the debug-console equivalent) in `image_files`, checks its decompressed
SHA256 against the entire disk, and preserves the previous ZIP under a dated
name. ZIPs are tracked in Git; raw `.img` files remain ignored. Extract the
image before flashing. This ZIP contains the whole disk, whereas the existing
`ouros-rpi5-boot-files.zip` contains only individual boot files.

The user has successfully booted the header-console image from a **32 GB USB
pendrive**, without a microSD card. The retained `-sd.img` filename does not
restrict the disk image to SD media. Firmware can load it from USB; kernel
USB disk access is a separate feature. On the header build `storageinit` now
uses experimental USB2 BOT when SD is absent/unclocked, then attempts FAT32
at `/boot`. The full read path passes QEMU tests and now has Pi sector/file-read
evidence. See [pendrive test instructions](usb.md). Run `storageinit` before
`usbtest`/`usbenum` on a fresh boot.

For a CP2102 connected with ordinary jumper wires to the **40-pin header**,
use the separate [header-console image and wiring guide](rpi5-header-uart.md):
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
Build it with `scripts/image-rpi5.ps1 -Console header`. The default image
described below continues to use the small 3-pin connector.
The header-console kernel has now booted on the user's real Pi 5 with working
commands and advancing uptime; see [hardware evidence](pi5-hardware-verification.md).
That observation applies to the tested header variant, not the default UART10 image.

The deliverable is `build/rpi5/OurOS-Pi5-experimental-sd.img`, a 128 MiB
partitioned disk image for Raspberry Pi 5 Model B. This is a real disk-image
file, unlike `ouros-rpi5.img`, which contains only the kernel.

**This is not a completed operating system, and hardware boot is untested.**
It contains the original kernel monitor and a Pi 5 firmware/UART boot candidate.
The header build's DTB RAM/reservations and physical pages now have Pi evidence;
see [memory.md](memory.md). MB/MiB display and the [RAMFS root](ramfs.md) now
also have physical Pi evidence. The latest [storage revision](storage.md) adds
read-only block I/O, FAT32 and `/boot` mount routing, tested on host/QEMU;
the Pi SD-handoff driver awaits its own capture. No storage writes, HDMI/GUI,
USB keyboard support, scheduling, virtual memory, networking or user applications
are implemented. [Wi-Fi diagnostics](wifi.md) do not implement association/IP.
RAMFS remains the volatile root; `storageinit` attempts a separate read-only
mount of the SD or supported USB boot FAT32 partition at `/boot`.
The current Pi port uses the dedicated 3-pin UART10 connector. Without a serial
adapter, you cannot observe or interact with this kernel on the board.

## Rebuild

From the repository in **Windows PowerShell**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1
```

This rebuilds the Pi kernel and boot package, constructs the disk image, verifies
it, then promotes the verified file to the delivery path. It writes only normal
files under `.dist/` and `build/`, never a physical disk. Existing image output is
replaced only after the new candidate passes validation.

Adjacent files:

- `OurOS-Pi5-experimental-sd.img.sha256`: image checksum.
- `OurOS-Pi5-experimental-sd.img.json`: layout and per-file hashes.

To recheck it without rebuilding:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-rpi5-image.ps1 -Image build/rpi5/OurOS-Pi5-experimental-sd.img -Manifest build/rpi5/OurOS-Pi5-experimental-sd.img.json
Get-FileHash build/rpi5/OurOS-Pi5-experimental-sd.img -Algorithm SHA256
```

## Layout

| Item | Value |
| --- | --- |
| Disk length | 134,217,728 bytes (128 MiB) |
| Sector size | 512 bytes |
| Partition table | MBR, one FAT32 LBA partition, type `0x0c` |
| Partition start | LBA 2048 (1 MiB alignment) |
| Partition length | 260,096 sectors (127 MiB) |
| FAT reserved sectors | 32 |
| FAT copies | Two, 2048 sectors each |
| Cluster size/count | 512 bytes / 255,968 clusters (FAT32 by cluster count) |
| Backup boot sector / FSInfo | Partition-relative sectors 6 / 7 |
| Volume label | `OUROSBOOT` |

`scripts/image-rpi5.ps1` is a deliberately narrow host-side FAT32 image writer,
not a target filesystem driver. It supports one root directory cluster, no
subdirectories, and only nonempty 8.3 filenames for this fixed payload. It rejects
unsupported input. The filesystem fields follow Microsoft's FAT specification;
the image is also read independently rather than trusting the writer alone.

Boot filenames are explicitly shortened, with matching `CONFIG.TXT` settings:

| Disk filename | Source |
| --- | --- |
| `OUROS.IMG` | Unchanged `ouros-rpi5.img` kernel bytes |
| `PI5.DTB` | Unchanged, pinned `bcm2712-rpi-5-b.dtb` bytes |
| `CONFIG.TXT` | Reviewed Pi config with only the two filenames replaced |
| `COPYING.TXT`, `LICENSE.TXT` | Unchanged upstream notices |
| `README.TXT`, `SOURCE.TXT` | Limitations and asset provenance |
| `FW43455.BIN`, `WIFINV.TXT`, `WIFICLM.BIN` | Pinned radio firmware, board NVRAM and CLM inputs |
| `WIFILIC.TXT` | Unchanged upstream vendor copyright/license notices |

No custom armstub, Linux kernel, EEPROM update or recovery binary is included.
The existing Pi EEPROM firmware must support the selected boot path. The exact
EEPROM version on the user's board has not been recorded.

The complete disk image includes the four radio assets; the separate
boot-files-only ZIP does not. `wififiles` verifies the three input hashes.
The new `wifiload` keeps the Wi-Fi CPU halted and uploads/reads back firmware
and packed board settings. Firmware startup/FullMAC/WPA3/IP remain unfinished;
see [the firmware-stage procedure](wifi-firmware.md). The image verifier checks
the radio revision, exact file lengths and SHA256 pins, including the notices.

## Verification and limits

The verifier checks the MBR and partition extent, FAT32 BPB and cluster count,
boot/FSInfo backups, FAT mirrors, reserved entries and free-space count. It then
uses the separately supplied 7-Zip reader to decode the partition table, test
the FAT filesystem, extract the file chains, and compare every file's length and
SHA256. It also checks the pinned DTB, expected config and AArch64 kernel header.
`scripts/test-rpi5-image.ps1` also passed rejection tests for a corrupt MBR,
a damaged FAT mirror, and altered kernel bytes. It mutates a test copy only.
These checks validate this fixed image format, not arbitrary FAT
volumes or all SD firmware behavior.

Existing kernel evidence covers A53/A76 QEMU monitor execution, EL1/EL2 firmware
handoff, deliberate exceptions and GDB startup checks. No QEMU Pi 5 model or
physical board test of the default UART10 image is claimed. The separate header
variant has the observed hardware results linked above. See [Pi 5 boot guide](rpi5-boot.md) for the register/firmware references,
wiring, expected output and remaining entry-state assumptions.

## Physical use

Unlike the raw kernel, this whole-disk `.img` has the layout expected by an image
writer. Writing it to a microSD card replaces its partition table and filesystem
contents and can destroy existing data. The build tools never write physical
storage; the user has separately deployed the header image for its boot test.
Identify the exact card, back up its contents and explicitly confirm the write
before proceeding. Do not select a disk based only on its advertised capacity.
The unused capacity on a larger card is not automatically partitioned or expanded.

The hardware acceptance criterion is a captured UART banner plus working `info`,
`cpuinfo`, `uptime` and `echo` commands on a real Pi 5. A blank HDMI display is
expected and does not establish success or failure. Until that test is complete,
this file must remain labelled experimental and hardware untested.
