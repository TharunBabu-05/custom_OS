# Larger persistent files and installed applications

Snapshot v4 raises each persistent file's limit from 8192 to **16384 bytes**.
It keeps sixteen shared file/directory slots, nested paths, synchronous commits,
two SHA256-protected snapshots and the existing 1 MiB `/data` partition. The
complete disk remains 128 MiB. This is bounded storage, not a general disk
filesystem or package manager. The root `/` remains volatile RAMFS.

The preceding Pi transcript verifies a responsive shell during a background
SPIN job, stop/collection with 130 and restored resources. That evidence is
retained in [the shell-job guide](shell-jobs.md). The v4 changes below pass native
and QEMU tests; **physical Pi v4 acceptance is pending**.

## Existing installation or new capacity

For an existing Pi 5 header-console installation, the
[boot-file update ZIP](../build/apps/OurOS-data-v4-boot-update.zip) updates
`OUROS.IMG`, the sample/test ELFs and `LARGE.BIN`. It includes a manifest,
hashes and copying instructions. Power off, back up the boot files on the PC,
check the previous kernel hash in its instructions, copy those files onto the
FAT boot volume, then safely eject. This preserves the separate `/data`
partition. It does **not** convert its format. Copy the kernel and applications
together: the new applications use syscall 43.

The [complete image ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
contains one flashable `.img` with an empty v4 volume. Extract before flashing.
Flashing replaces the existing `/data`; export files first if they matter.
Older full images remain archived flat in `image_files`.

| Mounted format | Shared nodes | Bytes per file | Directories |
| --- | ---: | ---: | --- |
| v1 | 8 | 4096 | Flat only |
| v2 | 16 | 8192 | Flat only |
| v3 | 16 | 8192 | Nested |
| v4 | 16 | 16384 | Nested |
| RAMFS `/` | 64 | 4096 | Nested, volatile |

Old v1/v2/v3 volumes stay readable and writable at their original bounds.
`datainit` never reformats, converts or resizes a volume. The FAT `/boot`
mount remains read-only in the OS.

## Pi commands

At the kernel monitor, enter one line at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
apprun /boot/DF.ELF
```

`DF.ELF` reports the actual mount format, writable file limit, used/total nodes
and read-only/volatile flags. For FAT, `nodes=0/0` means the count is unavailable,
not that the filesystem is empty. `max_file_bytes=0` means writes are disabled;
FAT reads still support larger boot assets. In `appshell`, use `df` or `df /data`.

On a **v4** volume, run:

```text
appterm 300 /boot/V4CHK.ELF
```

Expect `PASS: v4 16384-byte copy/hash...`, application exit 0 and resources
restored. The test needs four free nodes and unused `/data/.v4chk` and
`/tmp/.v4chk` names. It uses only its own fixtures, then removes them. It checks
copy/hash, a write across the former 8 KiB boundary, end-of-file overflow
preservation, RAM's smaller bound, replacement, mount queries and invalid
pointers. It refuses an older-format volume before creating fixtures.

To install and run a larger program on v4:

```text
mkdir /data/bin
appinstall /boot/BIGAPP.ELF /data/bin/big.elf
apprun /data/bin/big.elf
```

Skip `mkdir` if that directory already exists; installation requires a new
target. `BIGAPP.ELF` is 10664 bytes. It reads and checksums all 10000 constant
bytes, including bytes beyond offset 8192. Expect its PASS line and code 0.
An older-format volume refuses installation of this ELF, but can run it from
`/boot`. New executables retain the same 32 KiB image and 16 KiB guarded-stack
loader bounds; this phase raises stored-file capacity, not those VM bounds.

For a persistent binary file, enter `appshell`, then:

```text
cp /boot/LARGE.BIN /data/large.bin
sha256 /boot/LARGE.BIN
sha256 /data/large.bin
sync
exit
```

The two hashes should match. The destination must be new. Reboot **without
reflashing**, repeat initialization, then check the hash and run the installed
program again. Share the `V4CHK` output, `df`/`datainfo` and installed-program
result; the entire old command transcript is not needed. A same-boot success
and an actual physical reboot are separate observations.

## Explicit legacy backup conversion

Run `storageinit`, `datainit`, then `dataexport` on the existing installation.
Save the complete serial export to `backups/data-export.log`, including its end
marker. Personal backups and logs under `backups/` are ignored by Git.
On Windows, from the project directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/convert-data-backup.ps1 -Log backups/data-export.log -Output backups/old.bak
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/migrate-data-backup.ps1 -InputBackup backups/old.bak -Output backups/v4.bak -TargetVersion 4
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 4 -DataBackup backups/v4.bak
```

Use new output filenames; the tools refuse overwrite. Conversion preserves
every node's name, slot, parent, type, length and binary content, including
sibling names and sparse slot tables. Versions 1, 2 and 3 can convert to 4;
there is no downgrade or implicit same-version conversion. If the source is
already v4, use its validated backup directly without migration.

After explicitly flashing this backup-bearing image, run at the monitor:

```text
storageinit
datainit
datarestore /boot/DATA.BAK
datainfo
ls /data
```

Restore requires an empty healthy volume of the same format and validates the
whole archive before committing it. It never merges or automatically restores
on boot. `/boot/DEMO.BAK` is a public demonstration, not a personal backup.

## Format and transaction workspace

The directory remains sixteen 64-byte records. Like v3, each record contains
kind and parent-slot ID; the root is implicit. V4 reserves 16384 bytes per
slot, with zero unused entries, directory data and file tails. Geometry:

| Field | v4 value |
| --- | ---: |
| Directory bytes | 1024 |
| Payload bytes | 263168 |
| Bank sectors, including header | 515 |
| Superblock plus both banks | 1031 sectors |
| Normalized backup length | 263680 bytes |
| Fixed partition length | 2048 sectors |

The superblock uses version 4 and bank magic `OUROSBK4`; checksums cover headers
and full payloads. An update writes the inactive bank in this order:
invalidate header/flush, payload/flush/readback, committed header/flush/readback.
Ambiguous I/O keeps the old RAM view and disables further writes until reboot.

Mutations change at most one data slot and the directory. The kernel now keeps
one complete live snapshot plus a directory/one-file overlay instead of two
complete snapshot buffers. Mount validates both banks through that workspace
and revalidates a selected bank if necessary. Restore uses the required empty
volume's live buffer temporarily; reentrant reads and mutations refuse access
until validation/commit finishes. Failed restore clears it back to empty.
The Pi kernel's reserved memory ends at `0x1da000`, before the firmware DTB
at `0x200000`; simply doubling both old buffers would violate this constraint.

`STATFS=43` copies a checked 32-byte `uos_fs_info` for an existing path. It
reports per-mount limits; the original calls 0..42 keep their numbers and ABI.
Invalid, read-only, freed and guard-crossing output buffers are rejected before
any output copy. File read/write syscalls still transfer at most 512 bytes each.

## Verification

Native v1-v3 regressions pass. New v4 tests cover 2068 interruption outcomes
each for large writes, replacement and restore; all 515 truncated archive
input positions; corrupt archives; reentry refusal; 516 selected-bank reload/
flush failures; changed generation/digest; zero tails and strict geometry.
Host/guest backup interchange and v1/v2/v3 conversion preserve exact trees.

QEMU A53/EL1 and A76/EL2 pass V4CHK, full sixteen-slot DATACAP, checked mount
queries, larger installed-ELF execution, exact 16 KiB hashes and fresh-emulator
reopen. Independent host decoding validates both resulting snapshot banks;
boot/superblock/unused sectors stay unchanged. Legacy v1/v2/v3 stay writable
and reopen at old limits without conversion. Image verification independently
extracts all 66 boot files through five root clusters and checks hashes.
The builder/validator allow at most eight root clusters for this fixed payload.
Both profiles also restore the public FAT archive, reject restore over nonempty
data, export an actual 16 KiB file through the serial envelope and retain its
hash on fresh-emulator reopen. Host conversion plus the native guest-format
reader restore that real export byte-for-byte. Shell-job regressions still pass.

Reproduce with `scripts/test-persist-v4.ps1`, `test-persist.ps1`,
`test-data-backup.ps1`, `test-files.ps1`, `test-shell-qemu.ps1 -Data4` and
`test-shell-qemu.ps1 -Data4Backup`.
Logs are `build/phase-data4-*.log`. Test disks are regular scratch files.
These results do not establish physical power-loss recovery, general writable
FAT, unlimited file size, downloads or a package manager.
