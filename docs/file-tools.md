# Binary copy, checksums and sync

**Update:** [Pi acceptance now passes](../proofs/pi5-file-tools-2026-11-14.txt).
Later releases add [foreground shell programs](shell-programs.md) and
[file standard input/output](stdio.md).
The build hashes and sizes below describe this earlier file-tools release.

The previous [interactive shell now passes on Pi](../proofs/pi5-user-shell-2026-11-13.txt),
including shorter file replacement, data-file writes and resource cleanup.
The trailing backslash in `cat note.txt\` was an incomplete escape, correctly
rejected by the parser. The capture does not contain a power cycle.

This stage adds three shell commands:

| Command | Behavior |
| --- | --- |
| `cp <source> <new-target>` | Copy a complete binary file, at most 4096 bytes; refuse any existing destination; verify readback |
| `sha256 <file>` | Stream a file of at most 65536 bytes and print its SHA-256 digest |
| `sync` | Flush mounted `/data`, using its existing flush/write-through policy |

Run `storageinit`, `datainit`, `mmuinit`, `irqinit`, `timerstart`, then:

```text
apprun /boot/COPYCHK.ELF
appshell
write /tmp/note "copy-test"
cp /tmp/note /data/note.txt
sha256 /tmp/note
sha256 /data/note.txt
cat /data/note.txt
cp /tmp/note /data/note.txt
cp /boot/HELLO.ELF /data/copied.elf
sha256 /boot/HELLO.ELF
sha256 /data/copied.elf
sync
exit
apprun /data/copied.elf
fileinfo
heaptest
pagetest
timerstop
```

The self-test must PASS, each source/destination digest pair must match, and
the second copy to `note.txt` must report `shell: operation failed` because the
target exists. HELLO should run and the final handle count should be zero.
Choose unused destination names if these files already exist. Copy preserves
bytes, including zeros; it adds no newline and does not interpret ELF contents.
The existing ELF loader validates executable files when they are launched.

Power-cycle **without reflashing**, mount storage/data again and read/hash the
copied files to establish physical persistence. Reflashing replaces `/data`.
The root and `/tmp` remain volatile. `/boot` stays read-only, and `/data` still
holds eight flat files, each at most 4096 bytes. Large firmware files exceed
these commands' bounds. Checksums establish content equality, not publisher
authentication or package trust.

## ABI and failure handling

New calls: COPY=21 (source pointer/length, target pointer/length), FILEHASH=22
(path pointer/length, 32-byte writable digest destination), SYNC=23. COPY returns
bytes copied including zero for empty files; FILEHASH and SYNC return zero on
success. Existing negative error conventions apply.

Pointers and lengths are checked before I/O. Paths resolve against the caller's
cwd. Source reads hold an owned read handle; file leases and mount boundaries
remain enforced. COPY reads the entire source before one VFS publication,
refuses existing destinations and reads back every byte. Calls remain serialized
in the kernel, so the destination existence check cannot race another process.
Scratch memory is wiped and temporary handles close on success or failure.
If destination publication succeeds but readback fails, the file may exist;
an error does not promise rollback. After ambiguous storage failure, preserve
diagnostics and reboot before retrying. No automatic overwrite, format or resize
is performed.

COPYCHK tests empty and full 4096-byte binary files, refusal to overwrite,
self-copy, conflicting writers, invalid pointers, read-only boot, oversized
sources, SHA-256 known vectors and cleanup. The shell emulator test additionally
copies into `/data`, compares checksum output with the host SHA-256 implementation,
checks cold-boot data, and independently verifies snapshot bytes/hashes and
unchanged boot/source sectors. Physical copy/hash/sync acceptance now passes.

This is the bounded file-management stage. General writable storage, backup/export
off the Pi, process spawn/wait, packages and hardware/UI remain on the
[roadmap](remaining-os-roadmap.md).

## Build and validation

File-tools release full image: `image_files/OurOS-Pi5-header-experimental-sd.img`
(134217728 bytes). Its verified full-image ZIP is
`image_files/OurOS-Pi5-header-experimental-sd.zip` (839503 bytes).

Image SHA-256:
`89503b236509f28912259ff4ab0463aea9b6e7b7e2ede72ab7ac269719ba7080`.

QEMU Cortex-A53/EL1 and Cortex-A76/EL2 shell tests pass, including COPYCHK,
host-compared checksums, synchronous data writes and cold-boot reads. Native
file-handle/storage tests, all 24 ELF fixtures, 25000 ELF mutations, eight image
corruption cases and full disk/ZIP verification pass. The physical Pi capture
now passes this stage's acceptance. Detailed logs are
`build/phase-copy-{build,image,files,negative,elf,qemu}.log`.
