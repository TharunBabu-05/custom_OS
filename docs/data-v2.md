# Persistent capacity v2 and installing the shell

The [new Pi run](../proofs/pi5-usb-v2-capacity-2026-11-15.txt) now passes
DATACAP and usbcheck with 152 recovered stalls and USB online. Installed-shell
and physical power-cycle evidence remain pending. Current work is
[backup/restore](data-backup-restore.md); the earlier failure below is historical.

The [first Pi v2 test](../proofs/pi5-data-v2-io-failure-2026-11-15.txt)
failed DATACAP with code 3 during repeated chunk writes; installation and boot
reads failed afterward. Use the corrected image and acceptance sequence in
[USB storage reliability](usb-storage-reliability.md). The earlier QEMU results
below do not establish hardware acceptance. A new integrated device model now
reproduces the original driver's 32-recovery lifetime cutoff.

The [latest Pi capture](../proofs/pi5-pipelines-2026-11-14.txt) passes PIPECHK
(14 pipelines, 29 children collected), shell producer/consumer output, the
16384-byte binary stream digest, zero handles and heap/page restoration.
The `---` typo was rejected; the corrected `--` separator worked.

The two prompts have different command sets:

| Prompt | Purpose | Examples |
| --- | --- | --- |
| `>` | Kernel setup/diagnostics | `storageinit`, `datainit`, `mmuinit`, `irqinit`, `timerstart`, `appshell`, `appinstall` |
| `user:/$` | EL0 application shell | `sha256`, `run`, `runio`, `pipe`, `pipeio`, `status`, file commands |

After leaving the shell and running `timerstop`, enter `timerstart`, then
`appshell` before using shell commands again. The monitor now prints this hint
instead of a generic unknown-command message for shell-only names. MMU/GIC and
boot storage must already be initialized; on a fresh boot use the setup below.
The new Pi log starts with an empty generation-1 data volume. Files from an
earlier flashed image, such as `ctxt`, must be recreated; reflashing replaces
`/data`.

## New capacity

New disk images default to snapshot v2: **16 flat files, up to 8192 bytes each**.
The expanded shell now fits in persistent storage and can be installed, verified
and launched from `/data`. RAMFS stays limited to 4096 bytes per file; `/boot`
stays read-only. COPY and the installer now accept up to 8192 bytes while enforcing
the destination mount's actual limit. Standard stream writes retain 512 bytes
per syscall; handles assemble larger files through successive writes.

The new kernel also mounts existing v1 volumes at their original **8 files /
4096 bytes** limits. It writes their original layout and never converts, formats
or resizes them automatically. Build with `-DataVersion 1` for an explicit new v1
image if needed; the default build uses v2. Old kernels cannot mount v2 data.

`run`/`pipe` retain ten-second job budgets. `runio`/`pipeio` now use thirty
seconds to allow larger synchronous storage transfers. The enclosing shell
session deadline still applies.

## Pi acceptance

Flash the newest complete image, then run these commands at the kernel `>`
prompt, one at a time. DATACAP writes and removes test fixtures; run it on the
new **empty** data volume before installing files. It refuses a nonempty volume.

```text
storageinit
usbcheck
datainit
mmuinit
irqinit
timerstart
appterm 300 /boot/DATACAP.ELF
appinstall /boot/SHELL.ELF /data/shell.elf
appterm 60 /data/shell.elf
```

Wait for DATACAP to PASS; it makes many synchronous commits and can take time.
`datainit` should report `snapshot v2; files=16 max_file_bytes=8192`.
After `appterm`, the prompt must become `user:/$`. Enter:

```text
sha256 /boot/SHELL.ELF
sha256 /data/shell.elf
runio - /data/large.bin /boot/DATAGEN.ELF
sha256 /data/large.bin
ls /data
run /boot/HELLO.ELF
sync
exit
```

The shell checksum pair must match. DATAGEN emits exactly 8192 binary bytes
(byte values 0..255 repeated), so `large.bin` must have size 8192 and SHA-256:

```text
dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46
```

At the kernel `>` prompt, finish with:

```text
fileinfo
heaptest
pagetest
timerstop
```

Final handles must be zero and memory checks must PASS. Use unused destination
names if skipping DATACAP on an existing installation; neither copying nor the
installer overwrites an existing file. DATACAP refuses nonempty `/data` to avoid
modifying user files.

## Actual reboot check

Power off and restart **without reflashing**. Then at `>`:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
appterm 60 /data/shell.elf
```

At `user:/$`, check the stored shell and binary again with `sha256`, run HELLO,
then `exit` and repeat the handle/memory checks. This establishes physical
persistence separately from same-session reads. The current Pi capture does
not yet establish this power-cycle behavior.

## Layout and failure policy

The disk and partitions keep the same extents: 128 MiB total, read-only boot
FAT32 and a separate 1 MiB type-DA data partition at LBA 260096. V2 has a 1024-byte
directory, sixteen 8192-byte slots, a 132096-byte payload and two 259-sector banks
after the superblock (519 sectors used). V1 retains a 512-byte directory,
33280-byte payload and two 66-sector banks (133 sectors used).

Superblocks identify the exact version, slot count, file bound and bank size;
all geometry is checked against one of these two profiles. Bank magic is
`OUROSBK1`/`OUROSBK2`, with matching version and payload length. Each commit
invalidates and flushes the target header, writes/flushes/reads back the payload,
then commits/flushes/reads back the checked header. The previous bank remains
untouched. Checksums, generations, names, duplicate slots and unused bytes are
validated before selecting a volume. Ambiguous I/O failure retains the old RAM
view and refuses more writes until reboot. This remains the existing complete-
snapshot design; larger capacity costs more I/O per update.

Buffers remain bounded static kernel storage, with the image linker enforcing
the firmware DTB boundary. No new heap allocation class, kernel formatter or
general writable FAT driver is introduced. Nested persistent directories,
larger files/volumes, export/migration, installer preservation and physical
power-cut testing remain on the [roadmap](remaining-os-roadmap.md).

## Build and validation

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
Verified full-image ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip`.
The current SHA-256 is in
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img.sha256`; the original v2
image has been preserved as a timestamped archive.

The original v2 kernel was 421604 bytes, with reserved end `0x1bd000` below the firmware DTB
at `0x200000`. Its enlarged snapshot buffers are in the reserved kernel range;
disassembly confirms mount uses a small stack frame and clears the volume in
place. SHELL.ELF is 4480 bytes; DATACAP is 2496 and DATAGEN 464. All 36 sample/test
ELFs pass the parser checks and 25000 deterministic mutation cases.

Native tests pass both layouts, including 272 v1 and 1044 v2 update failure
cases (torn writes/ambiguous flushes), original mount diagnostics, 16-file/8192-
byte bounds, corruption fallback and independent PowerShell fixtures. File
handle failed-commit/remount/cleanup tests and eight image-corruption/profile
rejections pass. The root/file corruption fixture now derives its cluster mask
from the actual image so the larger boot directory still creates a true crosslink.

QEMU A53/EL1 and A76/EL2 pass v2 capacity, installed-shell execution after a
fresh emulator boot, independent comparison of every installed ELF/binary byte
and bank checksum, monitor hints, unchanged boot/superblock/unused sectors, and
v1 mount/write/read/limits without conversion. Both pipeline and previous
application/tools profiles pass using version-aware independent disk decoders.
Tests only write regular scratch files or in-memory copies; delivery/archive
hashes remain unchanged. Logs: `build/phase-data2-*.log`.

New physical Pi v2 acceptance remains pending. Previous full images/ZIPs and
the explicit v1 compatibility archive remain preserved in `image_files`.
No physical device was flashed and no Git commit/push was performed.
