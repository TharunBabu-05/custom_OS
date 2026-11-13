# Owned file handles and cooperative task I/O

The [latest Pi excerpt](../proofs/pi5-file-task-io-verified-2026-11-13.txt)
now verifies filetest, taskiotest before/after USB attachment, automatic close,
zero remaining handles and heap/page restoration with MMU/timer enabled.
The next milestone is [EL0 applications](user-applications.md).

This phase gives kernel applications an open/read/write/seek/close interface
over the existing RAM root, read-only `/boot`, and persistent `/data` routes.
Each open handle has its own offset, access mode and owner. This is the next
application foundation after cooperative task contexts and storage writes.
It does not introduce loaded programs, EL0 processes, system calls or preemption.

The [Pi write/sync capture](../proofs/pi5-usb-data-write-sync-2026-11-13.txt)
confirms that the previous USB fix now mounts `/data`, writes and reads
`hello-after-reboot`, and completes `sync`. The drive reports WCE=0; one BOT
stall was recovered, and the data snapshot advances from generation 1 to 2.
There is no second boot banner in that capture. A repeated `storageinit` or
`datainit` in the same session does not prove persistence across a power cycle.

## Interface and ownership

There are 16 handle slots. Successful opens return opaque tokens containing a
generation, so closing a file and reusing its table slot does not make an old
token valid again. A nonzero owner identifies the monitor or a cooperative
task. Another owner cannot use or close that owner's handle. Task identities
must remain distinct when a scheduler slot is reused.

Multiple read handles may share one file, each with an independent offset.
A write handle holds an exclusive lease: it cannot coexist with another
reader or writer for that file. Open handles pin the canonical file path.
External path-based writes, touches and removals cannot change a pinned file;
removing a pinned ancestor directory is also refused. Closing the final handle
releases the pin. Aliases are resolved before lease checks, and comparisons
respect path components: a lease for `/tmp/a` does not reserve `/tmp/ab`.

These checks are ownership rules inside a trusted, single-core kernel. They
are not yet a user/process security boundary. Raw filesystem backends remain
kernel internals. All handle operations complete in foreground context without
yielding. Timer IRQ handlers neither access files nor modify the handle table.
Static scratch storage keeps whole-file updates off the guarded task stacks.

The monitor modes are:

| Mode | Behavior |
| --- | --- |
| `r` | Open an existing file for reading |
| `rw` | Open an existing file for reading and writing |
| `w` | Open for writing with create/truncate behavior |

The core exposes explicit READ/WRITE and CREATE/TRUNCATE flags. Directories
cannot be opened as files. `/boot` remains read-only, including attempts to
create or truncate a file. An unavailable `/data` mount remains unavailable.
The handle layer does not format or mount a disk automatically.

Read access is bounded to the existing 64 KiB VFS file limit; writable RAM and
data files remain limited to 4096 bytes. A successful write starts at the
current offset and preserves bytes after the replaced range. It advances the
offset by the number of bytes written. Seek accepts a signed offset relative
to `set`, `cur`, or `end`, and only positions from zero through the file's
current end are permitted. Append and sparse files are outside this phase.
Zero-length operations do not extend a file.

Argument, ownership, bounds, lease and backend failures leave the handle's
offset unchanged. A failed persistent commit retains the data filesystem's
existing failure policy: further writes require a reboot. Successful `/data`
updates already commit synchronously; closing a handle is not a substitute
for the volume's durability checks. Owner cleanup releases remaining handles
on explicit exit, implicit return and scheduler cancellation.

## Monitor commands

| Command | Purpose |
| --- | --- |
| `fopen <path> r\|rw\|w` | Open a file and print its handle token |
| `fread <handle> <count>` | Read 1–512 bytes and advance that handle's offset |
| `fwrite <handle> <text>` | Write text at the current offset |
| `fseek <handle> <signed-offset> set\|cur\|end` | Move the current offset within the file |
| `fclose <handle>` | Release the handle and its path lease |
| `fileinfo` | Inspect handle usage and limits |
| `filetest` | Exercise the handle interface using volatile RAM files |
| `taskiotest` | Exercise file ownership and automatic cleanup in cooperative tasks |

Handle values are generated at runtime. Substitute the token returned by
`fopen` wherever `<handle>` appears below; do not type the angle brackets.

First test a volatile file:

```text
fopen /tmp/fd-demo.txt w
fwrite <handle> hello-file-handles
fclose <handle>
fopen /tmp/fd-demo.txt r
fread <handle> 5
fseek <handle> 0 set
fread <handle> 32
fclose <handle>
cat /tmp/fd-demo.txt
rm /tmp/fd-demo.txt
fileinfo
```

The first read should return `hello`; seeking back to zero permits reading
the complete text. Close the write handle before opening a reader. Attempting
`write` or `rm` on the same file while a handle pins it should report that it
is busy. A closed token should be rejected if used again.

## Pi acceptance run

After flashing the new full image and cold-booting, run these commands one at
a time, waiting for each prompt:

```text
mmuinit
irqinit
timerstart
filetest
taskiotest
fileinfo
storageinit
datainit
datainfo
cat /boot/CONFIG.TXT
storagetest
filetest
taskiotest
fileinfo
heaptest
pagetest
timerinfo
timerstop
```

`filetest` uses RAM files; it does not write `/data`. `taskiotest` requires the
MMU and running timer IRQs, just as the existing cooperative task tests do.
Check that the tests report PASS, the monitor returns, and handles belonging
to exited tasks have been released. Do not start the optional USB enumeration
tests before `storageinit`, because those tests consume controller state.

Test the actual persistent handle route separately:

```text
fopen /data/fd-demo.txt w
fwrite <handle> saved-by-file-handle
fclose <handle>
fopen /data/fd-demo.txt r
fread <handle> 64
fclose <handle>
sync
datainfo
fileinfo
```

Then power-cycle without reflashing. Capture the new boot banner and run:

```text
storageinit
datainit
datainfo
cat /data/fd-demo.txt
fileinfo
```

Expect `saved-by-file-handle`. A reboot creates a fresh handle table, so old
handle tokens are not retained; the saved file contents are what this check
must establish. Reflashing the full image replaces `/data`, so it cannot serve
as this persistence check. If mounting or an update fails, preserve `datainfo`
and `storageinfo` output and stop further data writes in that session.

## Validation status and remaining application work

Native handle tests pass for independent offsets, owner/stale tokens, leases,
table exhaustion before mutation, bounds, readonly boot access, memory and
backend failures, persistent writes/remount and cleanup. The source image
remains unchanged; only an in-memory test disk is written. Existing storage
and snapshot-failure regression tests also pass. New Pi acceptance remains
pending; the earlier hardware capture establishes the underlying mount/write/
sync behavior described above.

QEMU A53/EL1 and A76/EL2 checks pass across two cold boots per profile:
handle writes/seek preserve the expected committed file, monitor handle 48
survives repeated task-I/O tests, six handles auto-close per run, stale and
readonly access fail, and heap/pages restore. Independent host decoding verifies
both snapshot headers and payload hashes, generation 4 and exact contents.
Boot files, MBR, data superblock and unused sectors remain unchanged. Existing
scheduler/refusal/register/IRQ/fatal-fault tests also pass through EL1/EL2 entry.

Reproduce with `scripts/test-files.ps1`, `scripts/test-files-qemu.ps1` and
`scripts/test-tasks-qemu.ps1 -Handoff` after building the handoff-test kernel.
Logs: `build/phase-files-final-native.log`, `phase-files-qemu.log`,
`phase-files-task-regression.log`, `phase-files-storage-native.log`,
`phase-files-image.log` and `phase-files-image-negative.log`.

The full image is 134217728 bytes (128 MiB), ZIP 826133 bytes, and Pi kernel
397028 bytes. Kernel/BSS/stack reservation ends at `0x161000`, below the DTB
at `0x200000`. Full disk SHA256:

```text
927d8b20ebb42e3f74ede5a9687b76c12ac21f638e25997f255caa81cc9e1e5b
```

Both partitions, FAT/files, kernel header and decompressed ZIP hash verify.
Corrupt MBR/FAT/kernel/data extent/data payload and wrong-console images are
rejected. Prior full IMG/ZIP are preserved flat in `image_files`, including
`OurOS-Pi5-header-experimental-sd-20261113-012649-655.img` and
`OurOS-Pi5-header-experimental-sd-20261113-012653-423.zip`.

The next application steps are executable loading, a user address space,
EL0 entry and a syscall interface, followed by broader task creation,
blocking I/O and preemptive scheduling. File handles prepare a stable API for
that work; built-in monitor commands and cooperative test workers remain
kernel code. The full subsystem list is in [os-status.md](os-status.md).
