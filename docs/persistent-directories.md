# Persistent directories: data format v3

The [new physical Pi capture](../proofs/pi5-persistent-directories-2026-11-15.txt)
now passes DIRCHK with code 0 and resource restoration. Nested creation/cwd/
listing, binary move/hash, path/type guards and cleanup are verified for that
run. Physical reboot persistence, nested installed ELF execution and serial
export still require separate capture. No new image is needed for those checks.

The [latest Pi excerpt](../proofs/pi5-file-rename-2026-11-15.txt) passes MVCHK on
v2: binary file renames, aliases, no overwrite, leases and syscall pointer
checks, with resources restored. This phase adds persistent directories and
nested files to `/data`; the narrower hardware evidence is recorded above.

New images default to **v3: sixteen total nodes, files or directories, with
8192 bytes per file**. The root itself uses no slot. `mkdir`, `cd`, `ls`, `cat`,
`write`, `rm`, `rmdir`, `mv`, `cp`, file handles, program loading and the small
ELF installer now work with nested persistent paths. Equal filenames in
different directories are allowed. File `mv` can move between directories on
the same mount. This directory-stage release did not add directory moves,
replacement or cross-mount moves. Existing v1/v2 volumes still mount and retain their flat layout
and original bounds; the kernel never reformats or upgrades them automatically.

The RAM root remains volatile. Use `/data/home/user` for persistent user files;
`/home/user` is still in RAM. `/boot` remains read-only.

The subsequent [atomic update phase](atomic-file-updates.md) now adds explicit
regular-file replacement and validated executable updates on the same v3
format. The figures and DATA.BAK example below describe the directory release.

This acceptance image also includes `/boot/DATA.BAK` containing the public
automated test tree (projects/archive/bin and sample files). It is not your
personal backup and is never restored automatically. The normal fresh volume
is empty; the test below creates and removes its own scratch directory.

## Pi acceptance

Before flashing, save any files you want to retain using the backup procedure
below. A full-image write replaces `/data`. Boot the newest v3 image, then run
these individually at the kernel `>` prompt:

```text
storageinit
usbcheck
datainit
mmuinit
irqinit
timerstart
appterm 300 /boot/DIRCHK.ELF
```

`datainit` must report `snapshot v3; nodes=16 max_file_bytes=8192`.
Wait for DIRCHK's explicit PASS and `state=exited code=0`. It needs six free
slots and refuses a preexisting `/data/.dirchk`; on success it removes its
scratch tree. A failure should be captured with `storageinfo` and `datainfo`.

Create a small persistent tree using unused paths:

```text
mkdir /data/home
mkdir /data/home/user
write /data/home/user/note.txt hello-from-tree
ls /data/home
cat /data/home/user/note.txt
appshell
```

At `user:/$`:

```text
cd /data/home/user
pwd
sha256 note.txt
mv note.txt renamed.txt
sha256 renamed.txt
stat renamed.txt
sync
exit
```

The hashes must match and stat should report 15 bytes. `rmdir` refuses a
nonempty directory or an active working directory. Open-file leases still
protect paths. Missing parents, walking through a file and wrong file/directory
operations are refused. No recursive deletion is provided.

At `>`, install the shell under a persistent directory:

```text
mkdir /data/apps
appinstall /boot/SHELL.ELF /data/apps/shell.elf
appterm 60 /data/apps/shell.elf
```

Compare `sha256 /boot/SHELL.ELF` and `sha256 /data/apps/shell.elf`, then `exit`.
Check `fileinfo`, `heaptest`, `pagetest`, `storageinfo`, `datainfo`, `timerstop`.
Handles should be zero and memory checks must pass.

Restart **without reflashing**, repeat storage/data/MMU/IRQ/timer setup and run
`appterm 60 /data/apps/shell.elf`. Recheck the stored shell and note hashes. The
physical reboot check remains separate from same-session file operations.

## Preserve files from v1/v2

On the older kernel, enable plain serial session logging under the ignored
`backups` folder, run `dataexport` and wait for its END marker. Convert the log
as described in [backup/restore](data-backup-restore.md), then explicitly migrate
the archive on Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/migrate-data-backup.ps1 -InputBackup backups/pi5-data.bak -Output backups/pi5-data-v3.bak
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataBackup backups/pi5-data-v3.bak
```

Migration accepts v1/v2 archives, verifies every file, builds a v3 archive with
those files at its root, compares names/lengths/hashes and refuses an existing
output. It never writes a device or changes the source backup. Archive generation
starts at one; restoring uses the destination volume's next generation.

After deploying the new image, use `storageinit`, `datainit`, then
`datarestore /boot/DATA.BAK` on its empty v3 volume. Verify the files before
discarding either backup. V3 exports preserve the complete directory graph and
restore directly into empty v3 volumes. Version mismatch is refused; there is
no implicit in-place conversion or merge. New default images include a public
flat `/boot/DEMO.BAK` in the selected format for basic restore testing.

Backups, logs and images containing them hold your file contents. Keep personal
ones private. Checksums detect damage; they do not encrypt or authenticate them.

## Format and invariants

V3 keeps the 1 MiB type-DA partition and two 259-sector banks. Each payload is
132096 bytes: a 1024-byte node table plus sixteen 8192-byte data slots. Version
3 and `OUROSBK3` distinguish it from v2 despite their equal geometry.

Each 64-byte entry contains a zero-terminated 32-byte name field, u32 file
length at offset 32, u32 kind at 36 (1=file, 2=directory), u32 parent ID at 40
and zero reserved bytes at 44..63. Zero is the implicit root ID; other IDs are
slot+1. Unused entries/data and directory data slots must be entirely zero.
Directory length is zero; file tails are zero. Names retain the 31-byte ASCII
limit and mounted absolute paths must fit the 255-byte VFS limit.

Mount and restore validate kinds, parents, root reachability, cycles,
sibling-name uniqueness and path lengths as well as header/payload SHA-256.
Malformed graphs are never exposed. Runtime creates/moves/removes preserve
these invariants. Directory removal requires no children; regular-file rename
updates its name and parent in one snapshot. Whole-tree commits retain the
existing invalidate/flush/write/readback/commit ordering and first-failure
diagnostics. After ambiguous I/O, no more writes are accepted until reboot.

Storage still has a small fixed capacity and full-snapshot write amplification.
Directories consume slots even though their data areas are empty. Larger files,
volumes and a more scalable allocation scheme remain later work.

## Validation

Native tests cover nested paths, sibling filenames, binary bytes, tree export/
restore, full/deep trees and checksummed invalid graphs at both mount and
restore. All 3132 interrupted mkdir/file-move/rmdir outcomes remount a complete
old or new tree. The complete v1/v2 regression suite also passes. Independent
C/PowerShell tests cover legacy migration, nested archive paths and malformed
graphs. Handle tests check ancestor leases, aliases, cross-directory moves and
cleanup. DIRCHK exercises real EL0 calls and cwd protection.

QEMU acceptance is `scripts/test-shell-qemu.ps1 -Directories`; logs are
`build/phase-dirs-*.log`. It checks nested file/shell operations, 8192-byte
copy/hash, exported graph integrity and a nested installed ELF after a fresh
boot. A53/EL1 and A76/EL2 both pass. V1/v2 volumes also pass MVCHK, flat-file
updates and a fresh boot without conversion, while refusing directory creation.

The full tree roundtrip also passes on both profiles: guest export -> host
conversion -> new image DATA.BAK -> explicit restore -> DIRCHK on the populated
tree -> nested ELF execution and a fresh boot. The complete restored payload
hash matches the original, including metadata and padding. Boot/unused regions
remain unchanged. All 38 ELFs, 25000 parser mutations and eight image corruption/
profile checks pass. Physical DIRCHK now passes; reboot, nested installed ELF
execution and serial export remain pending as described above.

Delivered full image: 134217728 bytes; verified ZIP: 869337 bytes. Disk SHA-256:
`940d9292bc81b72a057b43eba19f7a19c581308d6effb05b46f63e663aa17420`.
Kernel: 433908 bytes, reserved end `0x1c4000` below DTB `0x200000`.
DIRCHK.ELF: 2496 bytes. Earlier complete images/ZIPs remain archived flat in
`image_files`; generated objects and scratch disk images are cleaned.
