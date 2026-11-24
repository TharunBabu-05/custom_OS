# Phase 5 extent-backed `/data` prototype

The v5 format provides **32 shared file and directory nodes** and accepts
files up to **32 KiB**. It allocates space from a shared pool inside the same
1 MiB data partition. A one-sector file update writes 11 sectors, including
both commit-header writes. A rename or delete changes only metadata.

The default image still uses v4. The same kernel mounts v1 through v4 with
their original limits. A v5 test requires a separate image, since the boot
updater only changes FAT boot files. Use the v5 image on a spare drive for
Pi testing. Keep the working v4 SanDisk as the source of any migration
archive.

## Disk layout

All indexes and lengths use little-endian integers. Sector numbers in the
table are relative to the partition's start. The standard image places the
partition at LBA 260096 with MBR type `0xda` and 2,048 sectors of 512 bytes.

| Relative sectors | Contents |
| --- | --- |
| 0 | `OUROSDAT` superblock with version 5, geometry, and SHA-256 of bytes 0 through 63 |
| 1–9 | Metadata bank 0. One `OUROSEX5` commit sector followed by eight index sectors |
| 10–18 | Metadata bank 1 with the same layout |
| 19–2047 | Shared data pool of 2,029 sectors. Each nonempty file occupies a contiguous extent |

Each metadata index holds 32 standard 64-byte directory records followed by
32 corresponding 64-byte extent records. The directory record gives the name,
logical length, kind, and parent. An extent record stores a 32-bit start
sector at byte 0 and 32 bytes of SHA-256 at byte 4. Its remaining bytes are
zero. Length determines the sector count. Empty files, directories and
unused slots have all-zero extent records. The last data sector is zero-padded.
The commit header includes the bank number, generation, index size, index
SHA-256, and a SHA-256 over its first 64 bytes.

Allocation protects every extent referenced by the mounted generation. If the
alternate bank is valid, its exclusive extents are available only after the
kernel durably invalidates that bank. The update sequence is invalidate and
verify, write and verify new data, write and verify the metadata index, then
publish and verify the new commit sector. An interruption can leave just one
valid generation. After remount the complete current or alternate generation
is selected based on their hashes. `datacheck` verifies both indexes and each
file hash. A device read error stops the mount with an I/O error. A
replacement needs a fresh contiguous run before its old extent
can be reused. A full or fragmented pool can refuse a write while nodes remain
free. The native capacity test stores 31 full-size files and one small file.

## Build and migrate

Build a verified v5 image on the PC. The script prints the path after `IMAGE:`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
```

For a real migration, use `dataexport` on the v4 system and convert its saved
serial transcript with `scripts/convert-data-backup.ps1`. Pass the resulting
`.bak` to the image builder using `-DataBackup` to include it as `DATA.BAK`.
The restore command accepts v4 and v5 archives on a healthy empty v5 volume.
It validates the full archive before writing, then publishes the import as
one generation. Compare file hashes with the source before using the migrated
copy. [Serial export instructions](data-recovery.md)

Extract the whole-drive `.img` from the prototype ZIP and select the spare
USB drive in the imager. Writing the image erases the selected drive.
[Header-console boot procedure](rpi5-header-uart.md)

The v5 image includes a public v4 demo archive at `/boot/DEMO.BAK` and the
32 KiB fixture `/boot/EXTENT.BIN`. After the shell starts on the spare drive,
type these commands one at a time.

```text
df /data
exit
datarestore /boot/DEMO.BAK
appshell
cp /boot/EXTENT.BIN /data/extent.bin
sha256 /boot/EXTENT.BIN
sha256 /data/extent.bin
exit
datacheck
reboot
```

After reboot, check the persisted file's hash and `datacheck` again. Use
`/boot/DATA.BAK` in place of the demo when testing a real migration. A v5
`dataexport` or `dataexport previous` sends a portable `OUROSBK5` archive with
fixed, padded data slots and no disk addresses. The archive is 1,051,136
bytes before serial encoding. Wait for the end marker before conversion.

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-persist-v5.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-persist-v4.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-data-backup.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -Extents -Image $image
```

Set `$image` to the v5 image path from the build. The native tests simulate
I/O failures at each update boundary, torn sector writes and ambiguous cache
flushes. They also exercise invalid metadata, copy-on-write allocation,
32 KiB reads, v5 export and restore, and 264 interrupted v4 import outcomes.
The emulator scenario
imports the v4 demo, verifies the larger file after reopening, corrupts the
newest version of one file on a scratch copy, and checks recovery of the
previous generation. Both the Cortex-A53/EL1 and Cortex-A76/EL2 runs passed.
The legacy QEMU suite also reopened formats v1 through v4 at their existing
limits.
The Pi 5 booted the v5 USB image and confirmed 32 slots and 32 KiB files.
The physical migration, retained-hash and interruption checks remain open.
[Pi boot capture](../proofs/pi5-data-v5-boot-2026-11-22.txt)

The verified ZIP is `build/rpi5-header/OurOS-Pi5-header-extents-v5-prototype.zip`.
It includes the raw image, its SHA-256 digest, and the image manifest.
Raw image SHA-256
`c0034b9782888ee801866007325fb94c2a18ed47c0154a48abbb6fb45281b152`.

## Phase 31 File history and recovery

`datahistory /data/PATH` validates both metadata banks and their
file extents. It reports each generation's path type and, for a file,
its length and SHA-256. An invalid bank reports its status, with
the path marked unavailable. A valid bank without the path reports
it as missing. Use absolute, canonical paths within `/data`.

`datarecover /data/OLD /data/NEW` copies a regular file from the older
valid generation to a new path in the mounted generation. The
destination's parent directory must exist. The kernel checks the
mounted bank and stages the complete verified file before writing.
Recovery uses the existing ordered commit and consumes the older
bank. After it succeeds, the previously mounted generation becomes
the older snapshot.

Native tests pass for healthy and damaged banks, empty and 32 KiB
files, and injected failures across every recovery I/O boundary.
Both QEMU profiles verify the comparison, refusal to overwrite,
full-size recovery, and matching hashes after a cold remount. The
boot update passed preview, apply, readback and rollback on a copy
of the Phase 30 boot directory. Its ZIP SHA-256 is
`4399c62d3be3f59da615702a2dc5323e186ba72f17badd1326491b18d4e174e5`.

Apply the boot update from Windows PowerShell with the existing v5
boot partition mounted as `D:\`. A Phase 30 boot drive should show
one changed file, `OUROS.IMG`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase31-data-recovery-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$` enter these
commands one at a time, waiting for each prompt. The directory
should be new.

```text
mkdir /data/p31-demo
write /data/p31-demo/live.txt older
write /data/p31-demo/live.txt current
exit
```

At the `>` monitor prompt, compare the two copies, recover the
older one and check the commit.

```text
datahistory /data/p31-demo/live.txt
datarecover /data/p31-demo/live.txt /data/p31-demo/recovered.txt
datahistory /data/p31-demo/recovered.txt
datacheck
reboot
```

Before recovery, the mounted file should show 7 bytes and the
previous file 5 bytes. After recovery, the recovered file's digest
should match the earlier 5-byte version. After reboot, check both
files and the mounted generation.

```text
cat /data/p31-demo/live.txt
cat /data/p31-demo/recovered.txt
exit
datacheck
```

Expect `current`, `older`, and `matches_mounted=yes`.
