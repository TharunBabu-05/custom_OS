# Atomic file publication and executable updates

The Pi's [DIRCHK result](../proofs/pi5-persistent-directories-2026-11-15.txt)
passed. This phase builds on it; the previous hardware checklist does not need
to be repeated to continue development. New behavior is tested locally before
delivery. Physical acceptance of these new operations remains pending.

The [new Pi excerpt](../proofs/pi5-file-publication-2026-11-15.txt) shows a
replacement command completing and shell cleanup. Its target was `not.txt`,
while `cat` queried `note.txt`; that different file correctly retained the old
contents. The target was not read, so overwrite/appupdate remain unverified
physically. Development continues with [application exec](application-exec.md).

`replace <source-file> <target-file>` publishes a prepared regular file at the
target path. It consumes the source name and replaces an existing regular
target, or creates the target if absent. Both paths must belong to the same
writable filesystem; their parent directories must already exist. RAMFS and
all three persistent data formats support it. `mv` continues to refuse overwrite.

For example, in either the monitor or `appshell`:

```text
write /data/note.txt old-version
write /data/save.tmp new-version
replace /data/save.tmp /data/note.txt
cat /data/note.txt
```

The final contents are `new-version`; `/data/save.tmp` is gone. Use test names
that do not hold your files. This operation deliberately replaces its target.
The shell also supports relative paths and quoted arguments.

Open source or target handles block a changing operation, even for the same
owner. Canonical aliases are checked. A same-path operation is a no-op, except
that a failed persistent volume still refuses it. Directory sources/targets,
read-only mounts, cross-mount moves and invalid parents are refused before
mutation. Directory replacement, exchanges and cross-mount copy/delete are
not implemented.

The persistent backend updates both names and all affected file bytes in one
existing dual-bank commit. It reuses the source slot, zeroes the old target's
entry/data, and does not need another free slot. An interrupted commit can
recover the complete prior snapshot (both names and old target intact) or the
complete new snapshot (source consumed and new target intact). It cannot
publish a half-renamed tree. An ambiguous I/O failure retains the old RAM view
and latches further writes off until reboot. The on-disk format is unchanged;
v1/v2 mounts retain their original flat layout and size limits.

RAMFS reuses the source buffer and releases the old target buffer. Its
namespace change is serialized in the kernel, but RAM contents remain volatile.
Atomic replacement does not make RAM files survive a reboot. Snapshot atomicity
also depends on the device honoring the flush/write-through behavior; software
fault models do not substitute for physical power-cut testing.

## Updating a stored application

At the monitor, `appupdate <source ELF> </data/existing-file>` validates the
entire source ELF before changing the existing target. It checks the target's
mount, regular-file type, size bound and open-file leases. The full contents
and length change in one snapshot commit, followed by synchronization and full
readback comparison. Invalid ELF input leaves the old target untouched.
`appinstall` keeps its create-only behavior. Neither command formats a disk.

Example with unused paths on a v3 volume:

```text
mkdir /data/apps
appinstall /boot/HELLO.ELF /data/apps/demo.elf
appupdate /boot/ECHO.ELF /data/apps/demo.elf
appexec /data/apps/demo.elf updated-application
```

Execution uses the existing MMU/timer setup. `appupdate` accepts nested v3
paths; v1/v2 require flat paths. The maximum stored executable is the mount's
existing file limit (v1: 4096 bytes; v2/v3: 8192). Updating a larger program
does not expand the filesystem. Source and target must be different paths.
An I/O error after a commit may mean the new version reached disk even if the
command reports failure; inspect the volume after reboot rather than retrying
on a failed mount. `replace` and `appupdate` do not retain a permanent version
history. The previous bank can be reused by later commits; export backups for
durable recovery copies.

`appupdate` is a bounded local executable update, without package manifests,
signatures, dependencies, downloads or multi-file transactions. Structural ELF
validation does not authenticate its publisher.

## ABI and verification

`PUBLISH=30` appends to the ABI. Arguments are source pointer/length and target
pointer/length, using the same bounded string checks and distinct error results
as `RENAME=29`. Existing `REPLACE=20` still writes supplied bytes to a single
file; its meaning is unchanged. The runtime wrapper is `uos_publish`.

`REPLCHK.ELF` exercises real EL0 calls: RAM/data binary and empty contents,
source consumption, both path leases, aliases, no-overwrite `mv`, read-only,
cross-mount and directory refusals, bad ranges and embedded NUL rejection. It
needs two free `/data` slots and refuses preexisting `.replace-src` and
`.replace-dst` scratch files. After the established boot setup, its only new
hardware self-test command is:

```text
appterm 300 /boot/REPLCHK.ELF
```

Local checks:

- `test-persist.ps1`: all existing recovery tests plus 4720 interrupted-I/O
  publication outcomes across v1/v2/v3, comparing the entire old/new payload.
  Covers maximum binary sizes, empty files, full tables and freed-slot reuse.
- `test-files.ps1`: actual VFS and heap/handle implementations; leases on both
  names, RAM buffer reuse, full RAM node table, mount/path/type refusals and
  unchanged handle/heap/page counts.
- `test-shell-qemu.ps1 -Replace`: real USB2/SCSI filesystem and EL0 operations
  on regular scratch disks, invalid-ELF rejection preserving the old hash,
  validated application upgrade, monitor/shell replacement, 8192-byte binary
  publication, exported tree validation and execution after a fresh emulator
  boot. A53/EL1 and A76/EL2 profiles, plus legacy v1/v2 update/reboot coverage.

Delivery images stay untouched during these tests. Logs are
`build/phase-replace-*.log`. The verified full image and full-image ZIP are
archived flat in `image_files`, preserving previous deliveries.

All checks above passed for this delivery, along with all 39 delivered ELFs,
the ELF parser mutation suite and eight image corruption/profile checks.
The full image is 134217728 bytes; its verified ZIP is 865114 bytes.
Image SHA-256:
`d743a08e0b77a2997f32c1493527065a4949f6a1ad99cc65591efec0b8e33a38`.
Kernel: 433908 bytes, reserved end `0x1c4000` below the firmware DTB at
`0x200000`. SHELL.ELF is 5352 bytes; REPLCHK.ELF is 2624 bytes.
The image starts with an empty v3 data volume and includes public DEMO.BAK;
it does not contain files created on the physical Pi. Use the established
[export/restore workflow](data-backup-restore.md) when preserving those files.
