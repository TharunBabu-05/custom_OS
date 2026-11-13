# ELF applications, EL0 processes and the user runtime

**Update:** [Pi acceptance now passes](../proofs/pi5-el0-applications-2026-11-13.txt)
for this stage, including installation and same-session execution. A physical
power-cycle check is still outstanding. The current image adds
[user utilities, arguments, directory queries and sleep/wake](user-tools.md);
the artifact details below describe this earlier eleven-program phase.

The [Pi file/task-I/O capture](../proofs/pi5-file-task-io-verified-2026-11-13.txt)
passes `filetest` and both cooperative `taskiotest` runs, before and after USB
storage attachment. The file table returns to zero open handles and heap/page
checks pass. The new phase builds on that verified foundation: bounded ELF
loading, private EL0 pages, a syscall ABI, timer-preempted foreground batches
and a small original user runtime. This EL0 path now has its own passing Pi run.

This phase implements the first application and installation foundations.
The remaining filesystem, package and local-device work is tracked in
[remaining-os-roadmap.md](remaining-os-roadmap.md). The available hardware test
setup currently has only the serial console.

## What runs

`apprun` starts one to three static AArch64 ELF64 little-endian executables as
a foreground batch. Each process gets independently allocated backing pages
and a private user mapping subtree at virtual address `0x2000000000`, with up
to eight image pages (32 KiB) and four stack pages (16 KiB). Unmapped space
separates the bounded image and stack. Kernel identity mappings remain EL1-only;
user code, constants and data use distinct RX, RO/NX and RW/NX permissions.
Writable executable load pages are refused.

EL0 entry and exception return save the integer register context and user SP.
The existing 100 Hz timer preempts user execution and rotates runnable
processes. User `yield` also rotates the batch. Kernel syscall and driver work
complete synchronously; kernel execution is not preempted by this scheduler.
There is a two-second whole-batch execution budget. An infinite-looping user
program is terminated when the timer enforces that budget, and control returns
to the monitor. A user fault terminates that process; the batch can continue
with its other runnable processes.

Exit, fault and final batch cleanup close owned file handles, wipe/free user
pages and restore the kernel page-table root. `appinfo` reports process states,
exit codes, syscall counts, dispatches, preemptions and cleanup status. The
processes use the same owned kernel file API introduced in the previous phase.
Filesystem-created RAM files remain filesystem-owned until explicitly removed.

This is a bounded original OurOS execution ABI. It provides no Linux syscall
compatibility, dynamic linker, general POSIX environment, process spawning,
signals, pipes, background jobs, SMP or general-purpose memory allocator.
The user build disables floating-point/SIMD instructions; that context is not
part of this first runtime.

## ELF validation and system calls

The parser accepts static `ET_EXEC`, ELF64LE, machine AArch64, with a bounded
program-header table and file size at most 64 KiB. Load segments start on pages,
occupy disjoint pages inside the 32 KiB image arena, and have R, R/X or R/W
permissions. The four-byte-aligned entry must point into executable file bytes.
File/memory sizes, alignment, overflow and input bounds are checked before
loading. Unsupported interpreter, dynamic and TLS program headers are refused.
Section/debug tables do not drive loading or relocations. The runtime applies
no dynamic relocations. See the [ELF program-header specification](https://gabi.xinuos.com/elf/07-pheader.html)
and [Arm AAELF64](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
for the underlying file format; OurOS deliberately supports a narrower subset.

The version-1 ABI in `include/user_abi.h` selects calls through `x8`, passes
arguments in `x0..x5`, and returns a signed result in `x0` using `svc #0`.

| Call | Meaning |
| --- | --- |
| `EXIT` | Finish the current process with a bounded exit code |
| `WRITE` | Write at most 512 bytes to the serial console |
| `YIELD` | Rotate to another runnable process |
| `TIME_MS` | Read time from the architectural counter |
| `OPEN` | Open a path through an owned file handle |
| `READ` | Read at most 512 bytes into writable user memory |
| `WRITE_FILE` | Write at most 512 user bytes through a file handle |
| `SEEK` | Move within the existing file length |
| `CLOSE` | Release the current process's handle |

Every nonempty user pointer range is checked page by page against that
process's private mapping. Output buffers must be writable. Kernel addresses,
unmapped pages, overflows, excessive lengths and invalid/foreign file handles
fail before the corresponding kernel access. User buffers are copied through
bounded kernel buffers and translated physical backing pages. Unknown calls
return an error. `UOS_ERROR=-1` is the general failure result and
`UOS_EFAULT=-2` identifies an invalid user memory range.

## Bundled programs and commands

The build packages eleven original sample ELF programs in the read-only boot
partition. They are small enough for the bounded `/data` application installer.

| Program | Purpose |
| --- | --- |
| `HELLO.ELF` | Console output and normal exit |
| `COUNTER.ELF` | Counter/time/yield runtime exercise |
| `FILES.ELF` | File syscalls and a RAM file |
| `BADPTR.ELF` | Invalid user-buffer rejection |
| `LEAK.ELF` | Deliberately open handle cleaned up on exit |
| `FAULT.ELF` | Contained user fault |
| `KERN.ELF` | Kernel-address access blocked by permissions |
| `TEXT.ELF` | User text writes blocked by permissions |
| `XSTACK.ELF` | Stack execution blocked by NX permissions |
| `BUSY.ELF` | Finite computation exercised with timer preemption |
| `SPIN.ELF` | Infinite loop stopped by the batch timeout |

Commands:

```text
appinfo
apprun /boot/HELLO.ELF
apprun /boot/HELLO.ELF /boot/COUNTER.ELF /boot/FILES.ELF
apptest
appinstall /boot/HELLO.ELF /data/hello.elf
apprun /data/hello.elf
```

`apptest` runs normal, checked-buffer/cleanup/fault, mapping-permission and
preemption/timeout batches. It removes its test RAM file and checks global
page/heap/handle restoration. It refuses to overwrite a pre-existing
`/tmp/user-note.txt`; save or remove that test path first if appropriate.

`appinstall` accepts only an ELF source of at most 4096 bytes and a new flat
filename under mounted `/data`. It validates the ELF before writing, refuses
an existing target, commits synchronously, then compares every readback byte.
It never formats or repartitions a device. This is a local executable copy
installer; package metadata, dependency resolution, downloads, authentication
and a general OS installer are later work. It accepts no shell wildcards or
paths containing spaces. `rm /data/hello.elf` can remove a closed installed file.

## Fresh Pi acceptance

Flash the current complete image, use the established 115200 8N1 header UART,
and run one command at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
apptest
appinfo
taskiotest
fileinfo
heaptest
pagetest
appinstall /boot/HELLO.ELF /data/hello.elf
apprun /data/hello.elf
sync
timerinfo
timerstop
```

Expected results are the final EL0 test PASS, contained faults and a timeout
inside their deliberate test batches, `cleanup=restored`, a responsive monitor,
zero remaining open handles and passing memory checks. The deliberately failing
sample processes are test cases; an unexpected kernel `FATAL`, failed resource
restoration or unresponsive monitor is a failure and its serial output should
be preserved. Do not rerun a storage update after the device reports a failed
commit in that session.

After installation completes, power-cycle without reflashing and run:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
apprun /data/hello.elf
appinfo
fileinfo
timerstop
```

This check must load the executable from the installed data file after a real
new boot. Repeating `storageinit` without rebooting does not establish that
persistence. Reflashing replaces `/data`, so it is not this check.

Native parser and user-mapping tests pass; parser tests validate all eleven
sample executables, with 25000 malformed-header mutations and exact AP/PXN/UXN
descriptor checks. QEMU A53/EL1 and A76/EL2 pass ELF execution, console/file
syscalls, independent process contexts, timer preemption, checked pointers,
automatic handle cleanup, fault/timeout containment, and kernel/RX/NX permission
tests. After installation, HELLO runs from `/data` in a fresh emulator process.
Independent host decoding verifies both snapshot hashes and every installed
ELF byte; boot/superblock/unused sectors and the delivery image remain unchanged.
Existing EL1/EL2 task/register/IRQ/protection and storage failure checks pass.
The linked Pi capture now verifies execution/installation in one session.
Fresh-boot execution remains emulator evidence until a physical reboot capture.

## Delivered image and checks

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img`, 134217728 bytes.
Verified ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip`, 828984 bytes.
Kernel: 405220 bytes, reserved end `0x184000`, below firmware DTB `0x200000`.
Whole-disk SHA256:

```text
ffb3d1f6573540a474fadde3878d4472a269d8e8bb8d2c7d7aaa7478f136bd61
```

Logs: `build/phase-apps-final-package-qemu.log`, `phase-apps-final-qemu.log`,
`phase-apps-user-mmu.log`, `phase-apps-elf-native.log`,
`phase-apps-task-regression.log`, `phase-apps-storage.log`,
`phase-apps-file-regression.log`, `phase-apps-final-image.log` and
`phase-apps-final-negative.log`. The FAT root uses two linked clusters to hold
the boot assets and eleven ELF files; cycle/crosslink corruption is rejected.
The current image has concise phase-specific boot-file instructions.
Earlier full images/ZIPs remain archived flat in `image_files`.
