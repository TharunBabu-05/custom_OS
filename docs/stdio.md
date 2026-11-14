# File standard input and output

The [latest Pi capture](../proofs/pi5-shell-programs-2026-11-14.txt) passes
SPAWNCHK, the early-read terminal handoff, shell HELLO/ECHO, a child timeout and
handle/heap/page cleanup. SPIN's `child exit=124` is the expected ten-second
timeout. That capture does not exercise child Ctrl+C, ORPHAN/CAPCHK or a second
boot; those remain emulator checks or separate physical acceptance items.

This stage adds file streams for applications:

```text
runio <input|-> <new-output|-> <ELF> [arguments]
```

Use `-` for the default terminal/output setting. Inputs are existing readable
files; outputs must be **new** files. Names resolve from the shell's cwd.
`runio - /data/greeting.txt /boot/ECHO.ELF "hello from OurOS"` saves the child's
output, including ECHO's final newline. `runio /data/greeting.txt - /boot/CAT.ELF`
displays it. CAT with no argument, or with `-`, reads its file stdin.
Normal `run` and explicit CAT file arguments continue to work.

## Pi acceptance

After flashing the current full image, run these one at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
appterm 30 /boot/IOCHK.ELF
appshell
runio - /data/greeting.txt /boot/ECHO.ELF "hello from OurOS"
runio /data/greeting.txt /data/copy.txt /boot/CAT.ELF
sha256 /data/greeting.txt
sha256 /data/copy.txt
runio /data/copy.txt - /boot/CAT.ELF
runio - /data/greeting.txt /boot/ECHO.ELF blocked
status
run /boot/HELLO.ELF
sync
exit
fileinfo
heaptest
pagetest
timerstop
```

IOCHK must PASS. The two hashes must match; file input should display
`hello from OurOS`. The attempted overwrite should fail with status 127, leaving
the original file intact. HELLO must still print to the terminal. Final file
handles must be zero and heap/page tests must PASS. IOCHK deliberately tests an
invalid ELF and prints its rejection; it also tests a child fault and timeout.
Use unused output names on an already-used installation.

Power-cycle **without reflashing**, mount storage/data again and read/hash the
two files to establish physical persistence. Reflashing replaces `/data`.
RAM files under `/tmp` disappear on reset. `/data` still holds eight flat files
up to 4096 bytes; this stage does not expand the storage format.

## Stream contract

- File stdin is binary and bounded to 65536-byte sources. STDIN reads at most
  512 bytes per call and returns zero at EOF. Without a file stdin, it returns
  ENOTTY; interactive serial input continues to use READLINE.
- WRITE, the existing application output call, uses the captured file when one
  is configured. File bytes are preserved, including zeros and control bytes.
  Terminal output keeps the existing character filtering. Output files remain
  bounded to 4096 bytes. There is no separate stderr stream yet.
- An existing output is always refused. There is no implicit truncation or
  append. The executable is validated and loaded, then input opened, before
  creating output. Invalid arguments, missing input, incompatible leases or an
  invalid ELF do not publish an output. An ambiguous storage error during the
  final create can still leave a file; no automatic rollback is promised.
- Stream handles belong to the child and are not exposed as inherited parent
  handles. Shared input readers and exclusive output writers retain the existing
  filesystem leases. Exit, fault, timeout, Ctrl+C, failed setup and parent
  termination release owned handles through the process lifecycle.
- Writes commit through the existing synchronous filesystem policy. Output can
  be empty or partial on failure/cancellation; inspect `status` and the file.
  A backend stream read/write failure sticks: if the program subsequently exits
  zero, the kernel reports 74 instead. Explicit nonzero exits, fault 125,
  timeout 124 and cancellation 130 retain their own status.
- Invalid pointer/length calls fail before I/O or cursor changes and do not set
  that sticky backend-error flag. File stdin disables terminal READLINE for
  that process; this stage does not execute shell scripts from redirected input.
  Ctrl+C can still stop a foreground file-processing child.

These are foreground file streams. Pipes, append/overwrite operators, arbitrary
descriptor duplication/inheritance, background jobs, larger storage, permissions
and a complete POSIX stdio layer remain future work. Shell `runio` uses the same
127-byte command line, eight-word parser, ten-second child budget and three
total process slots. The three leading runio words leave room for the ELF and
at most four application arguments.

## ABI and tests

SPAWN_IO=26 takes a packed argv pointer in x0, a 1..30-second budget in x1 and a
pointer to `struct uos_stdio` in x2. That structure is exactly 512 bytes: input
and output are 256-byte NUL-terminated paths; an empty string keeps the default.
The kernel copies and validates all metadata before creating streams.
Existing SPAWN=24 behavior is preserved. STDIN=27 takes an output buffer in x0
and capacity 0..512 in x1 and returns a byte count or existing negative error.
WRITE=1 keeps its original ABI and routes through the child's selected output.

IOCHK checks empty/full binary copies, EOF, invalid pointers/capacities,
unterminated stream paths, no overwrite, invalid ELF/missing input, read-only
boot, conflicting writers, exhausted handles, input/output path leases, the
4096-byte output bound, sticky I/O failure and fault/timeout cleanup. IORAW is
the binary/error fixture. The emulator suite additionally checks shell capture,
host-computed binary hashes, Ctrl+C, terminal restoration, cold-boot data and
independently decoded snapshots with unchanged boot/superblock/unused sectors.

## Build and validation

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
Its full-image ZIP is `image_files/OurOS-Pi5-header-experimental-sd.zip`
(847130 bytes), verified by extracting and hashing the entire image.

Image SHA-256:
`894cb981714ae8cb21e39390895669313c50ac6097b7703219a90c54564f77df`.

There are 31 checked ELF files: SHELL is 3888 bytes and IOCHK is 3344 bytes;
all fit the existing 4096-byte persistent-file bound. The Pi kernel is 417508
bytes with reserved end `0x188000`, below firmware DTB `0x200000`.

QEMU Cortex-A53/EL1 and Cortex-A76/EL2 stream create/reopen tests pass, including
host-compared raw binary digests, overflow/fault/Ctrl+C cleanup, terminal
restoration and independent snapshot decoding. Both previous application/tools
profiles and the focused child/early-terminal-handoff regression pass. Native
file-handle, failed-commit/remount and baseline-restoration tests pass. All ELF
fixtures, 25000 parser mutations, strict cross-build and disk/ZIP verification
pass. Tests modify only regular scratch disks or an in-memory copy; the delivery
image and boot/superblock/unused sectors remain unchanged.

Logs: `build/phase-stdio-{build,image,elf,files,qemu,child-regression,regression}.log`.
New physical Pi stream acceptance is pending. Previous images/ZIPs are retained
in `image_files`; no physical device was flashed and no Git push was performed.
