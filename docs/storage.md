# Storage phase: block reads and a FAT32 boot mount

This revision adds a read-only block API, a bounded FAT32 reader, and routing
between the RAM root and `/boot`. Real QEMU virtio block I/O and native tests
pass. **Pi SD reads and the new SDIO query still require physical captures.**
The previous RAMFS revision now has physical Pi evidence from the user's
screenshots, including file creation, navigation, reads and MB/MiB summaries.

## Root layout

```text
/
├── README.txt
├── boot/          Read-only FAT32 mount after storageinit
├── dev/           Reserved directory; no device nodes implemented yet
├── etc/
│   ├── hostname
│   └── board
├── home/user/
├── mnt/           Reserved mount directory
├── proc/          Reserved directory; no process pseudo-filesystem yet
├── sys/           Reserved directory; no sysfs implementation yet
├── tmp/
└── var/log/       RAM directory; no automatic log writer yet
```

The RAM root remains writable and volatile. `/boot` is an initially empty,
protected mountpoint. Successful `storageinit` mounts the card's existing FAT32
partition there; file commands then cross the mount boundary. This does not
copy all card files into RAM or turn the RAM root into persistent storage.
Three seeded files still own three physical pages; directories use static nodes.

## Test on the Pi 5

Use the new experimental header image, retaining the existing CP2102 wiring
and 115200 8N1/no-flow-control terminal settings. Initialization is opt-in so a
storage failure does not prevent the monitor from starting. Run:

```text
storageinfo
storageinit
storageinfo
ls /
ls /boot
cd /boot
pwd
cat CONFIG.TXT
cat SOURCE.TXT
write CONFIG.TXT blocked
cd ..
pwd
wifiinfo
```

Expected on successful attachment:

```text
/boot: FAT32 read-only start_lba=2048 sectors=260096 ...
```

`ls /boot` should show the actual short filenames such as CONFIG.TXT, OUROS.IMG
and PI5.DTB. CONFIG.TXT should contain `kernel=OUROS.IMG`. The write attempt must
print `fs: read-only filesystem`. The source/README files come from the card,
while `/README.txt`, `/etc/board` and files under `/home` are RAMFS objects.

If attachment fails, capture **all `storageinfo` output and the boot log**.
Do not treat an unmounted empty `/boot` as successful card access. If the card
booted through USB storage instead of the onboard SD slot, the SD driver does
not automatically follow it. No USB mass-storage or NVMe driver is provided.

## Pi driver contract

`pi5_sd.c` selects the BCM2712 mmc0 host at CPU address `0x1000fff000`, translated
from the Pi's SD host node, not the separate Wi-Fi host. `sdhci.c` implements
polling PIO single-sector **CMD17 reads only**. It inherits the firmware's
selected SDHC/SDXC card, bus width, voltage, clocks and tuning. It does not reset
or power-cycle the card, negotiate voltage, reinitialise it, discover its RCA/CSD,
or implement card writes. Its block bound is the 32-bit sector address limit,
**not measured card capacity**. FAT reads are further bounded by the partition.

The driver rejects an absent, inhibited, unpowered/unclocked or CQE-enabled host.
If firmware deselects the card, gates clocks, resets it, or leaves incompatible
controller state, the first read fails with diagnostics. This is a deliberately
narrow firmware-handoff candidate rather than complete SD lifecycle support.
SDSC byte-addressed cards and SDUC/extended addressing are unsupported. Firmware
changes, hotplug and recovery after I/O failure require additional work.

Waits have architectural-timer deadlines plus bounded polling loops. A timeout,
CRC or card-status error disables further reads through that attachment. The
driver changes controller command/PIO/status registers, but never issues a
card write/erase command, writes data to the card FIFO, or changes power/voltage.
`storageinfo` reads controller registers; `storageinit` attempts the handoff.
No driver is probed automatically at boot.

## Shared block and FAT32 code

- `block_device`: a 512-byte single-sector read callback, bounds and capacity provenance.
- QEMU: modern virtio-mmio split queue with a single outstanding read; no write
  request type. Static queue/bounce buffers remain reserved even after timeout.
- FAT32: one MBR FAT32 partition, 512-byte sectors, 1/2 FAT copies, mirrored FAT
  checks, short 8.3 names, subdirectories and fragmented file chains.
- No GPT, exFAT, FAT12/16, long filename reconstruction, links, write journaling
  or FAT writes. File chains used by reads must match their advertised size and
  terminate correctly. Traversals are bounded to prevent loops.
- Monitor `cat` caps FAT file size at 64 KiB, streams in sector-sized chunks,
  and replaces non-text control/binary bytes with dots to avoid terminal escapes.
  It is intended for small text files, not dumping kernels or DTBs to the terminal.
- Two fixed routes constitute the initial VFS; there is no general mount table,
  file descriptors, permissions or concurrent access. `/boot` mutations are
  rejected before invoking RAMFS operations, including canonicalized paths.

Persistent file creation is a later storage-write milestone. It needs a writable
test partition, card-write protocol, metadata update ordering and failure tests.
This phase verifies the read path before modifying any on-card filesystem.

## Host and QEMU verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-storage.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-ramfs.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1 -StorageImage build/rpi5-header/OurOS-Pi5-header-experimental-sd.img
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1 -Handoff -StorageImage build/rpi5-header/OurOS-Pi5-header-experimental-sd.img
```

Interactive QEMU storage session:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-qemu.ps1 -StorageImage build/rpi5-header/OurOS-Pi5-header-experimental-sd.img
```

The image is attached as **read-only** to QEMU. The tests compare its SHA256
before and after. Native tests mutate an in-memory image copy only and test MBR
bounds, BPB validation, mirror errors, fragmented/nested chains, loops, short
chains, I/O failures, mount traversal and write rejection. A mock register host
exercises the exact Pi SDHCI command engine, legal register writes, preserved
bus/clock setup, CRC/R1/timeouts and CQE rejection. This does not emulate the
physical Pi controller or prove its firmware handoff works.

Previous physical RAMFS image is preserved locally as
`build/rpi5-header/OurOS-Pi5-header-ramfs-verified.img`, alongside its manifest
and checksum. Build scripts write regular files only; no physical card was
written by the agent.

## Primary references

- [BCM2712 SD host map](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)
- [Pi mmc0 and wifi0 aliases](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi)
- [SDHCI register definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci.h)
- [Broadcom host quirks](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci-brcmstb.c)
- [SD Association simplified specifications](https://www.sdcard.org/downloads/pls/)
- [Virtio 1.2, MMIO transport and block device](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
- [Microsoft-authored FAT format specification](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf)
