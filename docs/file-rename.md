# Atomic file rename and file details

The [new Pi run](../proofs/pi5-file-rename-2026-11-15.txt) now passes MVCHK
with code 0 and resource cleanup. The current phase is
[persistent directories/v3](persistent-directories.md). The build figures
below describe the archived rename-stage image.

The [latest Pi capture](../proofs/pi5-data-restore-2026-11-15.txt) verifies the
previous phase's DEMO.BAK restore: generation 2, two valid banks, a 31-byte text
file, an 8192-byte binary file and correct text readback. Binary hashes, serial
export, stored-shell execution and an actual physical reboot are still untested
in that capture.

This phase adds `mv <source-file> <new-path>` and `stat <path>` to both the kernel
monitor and the EL0 shell. The new `RENAME` syscall is also available to user
programs. It renames a regular file on the same writable filesystem and refuses
an existing destination. No directory moves, implicit move-into-directory,
cross-filesystem copy/delete, replacement or exchange operation is implemented.
`/data` remains flat; its format, capacity and archive layout are unchanged.

## Pi acceptance

Use the new full disk image. A full-image flash replaces `/data`; preserve files
using the [backup workflow](data-backup-restore.md) before a later update. If
using a backup, build with `-DataBackup` and restore it explicitly after boot.
The default delivery image has an empty v2 volume and public DEMO.BAK.

At the kernel `>` prompt:

```text
storageinit
usbcheck
datainit
mmuinit
irqinit
timerstart
appterm 120 /boot/MVCHK.ELF
```

Wait for the explicit PASS and `state=exited code=0`. The self-test needs two
free data slots and refuses preexisting scratch names. It checks RAM/data
binary contents, relative paths, same-name no-ops, no overwrite, open-file
leases, cross-mount/read-only/directory refusals and bad syscall pointers. It
removes its test files after success. On a failure, stop and save the process,
`storageinfo` and `datainfo` reports.

Test the monitor commands with unused names:

```text
write /data/before.txt hello-rename
stat /data/before.txt
mv /data/before.txt /data/after.txt
stat /data/after.txt
cat /data/after.txt
```

Expect `type=file bytes=12` and `hello-rename`. The old path should be absent.
Do not reuse names holding your own files: `write` replaces contents, while
`mv` always refuses replacement.

Run `appshell`, then at `user:/$`:

```text
cd /data
sha256 after.txt
mv ./after.txt ../data/final.txt
sha256 final.txt
stat final.txt
write keep.txt keep-this
mv keep.txt final.txt
cat keep.txt
cat final.txt
sync
exit
```

The hashes before/after must match. The second move must print
`shell: target exists; mv does not overwrite`, leaving both files intact.
For RAM files, moving between existing RAM directories is supported; moving
between `/tmp` and `/data` is refused. `stat` reports the real file size or
directory type; it does not invent permissions, timestamps or disk usage.

To check a moved executable, at `>`:

```text
appinstall /boot/SHELL.ELF /data/shell.elf
mv /data/shell.elf /data/cli.elf
appterm 60 /data/cli.elf
```

At `user:/$`, compare `sha256 /boot/SHELL.ELF` and `sha256 /data/cli.elf`, then
`exit`. At `>`, run `fileinfo`, `heaptest`, `pagetest`, `storageinfo`,
`datainfo`, `timerstop`. Handles should be zero and checks must pass.

Restart **without reflashing**, repeat storage/data/MMU/IRQ/timer setup and
run `/data/cli.elf` again. Verify `/data/final.txt` and the ELF hashes. This is
the outstanding physical persistence check; the posted restore log only shows
same-session behavior.

## Ownership and durability

VFS canonicalizes both paths, requires an existing source and destination
parent, checks the mount boundary and protects open-file paths. A file with an
open reader or writer cannot be renamed. Identical canonical names are a no-op
on a healthy volume, including while that file is open; an already failed data
volume still reports its retained I/O failure.

RAM renames reuse the node, byte buffer and allocation. The parent/name update
is serialized with other filesystem calls by the existing single-core kernel.
Persistent renames retain the same slot, contents and length, change the name
in staging, then publish one complete dual-bank snapshot. They work even when
all slots are occupied. Failure retains the old RAM view and refuses more
writes. A remount selects one complete old or new filename, never a partial
rename. Each update still writes a full snapshot; larger files and directories
need further filesystem work.

`RENAME=29` appends to the existing ABI without renumbering earlier calls. Its
arguments are source pointer/length and target pointer/length. Both ranges and
strings validate before mutation. Results distinguish missing source/parent,
existing destination, busy file, mount crossing, read-only filesystem and
directory source. Existing syscalls keep their behavior. `stat` uses the
existing STAT call and adds no new on-disk metadata.

Native tests check v1/v2 binary contents, no overwrite, full-directory slot
reuse and all 272/1044 interrupted write/read/flush outcomes for rename. Handle
tests check aliases, buffer/allocation identity, leases, mount/readonly/path
errors and the failed-volume no-op guard. `MVCHK.ELF` exercises the real EL0
syscall. Emulator acceptance is `scripts/test-shell-qemu.ps1 -Rename`.
QEMU A53/EL1 and A76/EL2 pass MVCHK, monitor/shell moves and stat, exact names
and digests in an independently decoded serial export, moved-shell execution
after a fresh boot and unchanged boot/unused sectors. A legacy v1 volume also
passes rename/reboot without conversion. All 37 delivered ELFs and 25000 parser
mutation cases pass; eight image corruption/profile checks pass. Logs:
`build/phase-rename-*.log`. New physical rename/stat acceptance is pending.

Current image is 134217728 bytes and its verified full-image ZIP is 860819
bytes. Disk SHA-256:
`84c2a7d294ba1701c97a532b5065b680be5fa6e2ea826014494a2a8608af3b17`.
Kernel: 433908 bytes, reserved end `0x1c4000` below DTB `0x200000`.
SHELL.ELF: 5176 bytes; MVCHK.ELF: 2576 bytes. Earlier full images/ZIPs are
preserved in `image_files`; known build objects and scratch disks are cleaned.

The next filesystem milestones remain nested persistent directories, larger
files and migration; replacement/transactional installation are separate work.
