# Two-stage application pipes and clearer path errors

Phase 29 extends `|` to four stages and keeps the explicit two-stage
commands below. [Current pipeline behavior and Pi test](platform-driver-plan.md#phase-29-process-capacity-and-pipelines)

**Update:** [Physical pipe acceptance now passes](../proofs/pi5-pipelines-2026-11-14.txt):
PIPECHK, streamed digest, shell pipelines and resource cleanup. The `---` typo
was corrected to `--`. Current images add [persistent capacity v2](data-v2.md)
and a monitor hint explaining `>` versus `user:/$`; artifact details below
describe the earlier v1-volume pipeline release.

The [latest Pi log](../proofs/pi5-stdio-2026-11-14.txt) passes IOCHK, captures
ECHO output in `/data/greeting.txt`, copies it to `/data/ctxt`, and restores all
file handles and heap/page counts. The later errors used different names:

| Entered | Why it failed | Correct command for those existing files |
| --- | --- | --- |
| `sha256 /data/copy.txt` | The copy was named `ctxt` | `sha256 /data/ctxt` |
| `runio /data/copy.txt - /boot/CAT` | Input name differs and executable lacks `.ELF` | `runio /data/ctxt - /boot/CAT.ELF` |

The `ELF rejected` line inside IOCHK is an intentional invalid-executable test,
followed by PASS. The capture shows successful same-session file I/O, not a
second boot. No storage-driver fix was needed for these path mismatches.

Missing checksum paths now report `shell: file not found: <path>`. Loader
failures name the attempted executable, and launch failures remind the user to
check ELF/input/output paths. The kernel does not silently guess or rename files.

## Pipe commands

The shell also accepts `LEFT | RIGHT`, with `< INPUT` on the left
and `> NEWFILE` on the right. [Shell examples](shell-workflows.md#pipe-and-redirection-operators)

```text
pipe <producer ELF> [args] -- <consumer ELF> [args]
pipeio <input|-> <new-output|-> <producer ELF> [args] -- <consumer ELF> [args]
```

The producer's stdout feeds the consumer's stdin through a 1024-byte binary
FIFO. Both applications run concurrently in private EL0 mappings; the parent
shell blocks until both finish. No intermediate file is used. Reads block when
empty and drain buffered bytes before EOF after producer termination. Writes of
at most 512 bytes block until enough space is available; successful writes are
complete. Early consumer termination returns EPIPE to the producer.

`pipeio` optionally opens a file as producer stdin and creates a new file for
consumer stdout, using the existing lease and no-overwrite rules. `-` means
no input file/default terminal output. Pipeline children cannot use serial
READLINE; Ctrl+C cancels both stages and returns to the shell. Ordinary typed
input is discarded while a pipeline owns the foreground.

## Pi acceptance

Flash the new image, then enter these commands one at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
appterm 30 /boot/PIPECHK.ELF
appshell
pipe /boot/ECHO.ELF "hello through pipe" -- /boot/CAT.ELF
pipe /boot/STREAM.ELF -- /boot/HASH.ELF
write /tmp/input "saved through pipe"
pipeio /tmp/input /data/piped.txt /boot/CAT.ELF -- /boot/CAT.ELF
sha256 /tmp/input
sha256 /data/piped.txt
pipeio /data/piped.txt - /boot/CAT.ELF -- /boot/CAT.ELF
pipe /boot/STREAM.ELF -- /boot/SPIN.ELF
```

Press **Ctrl+C** at the last pipeline. Expect `pipeline exit=130` and the shell
prompt. Then:

```text
status
run /boot/HELLO.ELF
sync
exit
fileinfo
heaptest
pagetest
timerstop
```

PIPECHK must PASS; its invalid-ELF, broken-pipe, fault and timeout cases are
deliberate. STREAM generates 16384 binary bytes. HASH prints SHA-256 of stdin:
`a1f259d4365ed4320c377ce26f5c8c56dcdc9a89e7b641bfd8eabfbbeac86654`.
The file checksum pair must match; final handles must be zero and heap/page tests
must PASS. Use an unused output filename if `piped.txt` already exists.

Power-cycle without reflashing and mount `/boot` and `/data` again to read/hash
the saved file. Reflashing replaces `/data`; this remains a separate physical
persistence acceptance item. RAM files disappear on reset.

## Bounds, status and cleanup

One foreground pipeline uses all three process slots: parent plus two children.
It requires an original/root process with no outstanding child and two unused
slots. Nested pipelines/spawn, background pipelines, longer chains and a public
general file-descriptor API remain future work. The `pipe` and `pipeio` commands
require one `--` separator and keep their eight-word combined limit. The `|`
syntax allows eight words per stage. All forms keep the 127-byte line limit.
Both stages share a 10-second budget with `pipe` or a 30-second budget with
`pipeio` and the operator syntax, subject to the enclosing session limit.

The return status is the consumer's nonzero exit, otherwise the producer's exit;
Ctrl+C returns 130. A producer ignoring EPIPE and returning zero reports 74 via
the existing sticky stream-error rule. Fault 125, timeout 124 and explicit
nonzero exits retain their meaning. This is not a complete POSIX job/signal model.
Partial output files can remain on failure, and no rollback is promised after
ambiguous storage errors. Existing output files are never overwritten.

Both ELFs and all stream metadata are validated before running either child;
input is opened before output creation. Failed setup closes partial handles
and frees mappings. Termination closes the appropriate FIFO endpoint, wakes a
blocked peer, and collects both children under the kernel page-table root.
Parent termination, session expiry and IRQ failure also collect both peers.
FIFO bytes and pending write buffers are wiped during cleanup. Drivers remain
synchronous; only user exception frames switch.

The shell is now larger than 4096 bytes and runs from `/boot` with up to two
image pages. It cannot be installed into current `/data`, whose eight flat files
still have the same 4096-byte limit. Other sample programs retain that bound.
The kernel's existing 32 KiB ELF/image arena is unchanged.

## ABI and tests

PIPE_RUN=28 takes producer argv pointer x0, consumer argv pointer x1, a 1..30
second budget x2, and optional `struct uos_stdio` pointer x3 (NULL for default
endpoints). It launches both stages and blocks the caller, returning the job
status or a negative validation/setup error. WRITE=1 and STDIN=27 use the pipe
for the respective child; existing file/terminal behavior remains elsewhere.
New negative results are EPIPE=-5 and ENOENT=-6 for missing copy/hash sources.

Native ring tests cover wrap/full/empty states, failed-write preservation, EOF
drain, endpoint closure/wiping and 10000 FIFO cycles. PIPECHK covers binary
digests, empty/partial EOF, repeated slot reuse, delayed readers/writers,
invalid pointers/setup, insufficient slots, file endpoints, nested rejection,
early reader exit, producer/consumer faults, blocked timeouts and cleanup.

## Image and validation

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
Full-image ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip` (853332 bytes),
verified by extracting and hashing the entire image. Extract before flashing;
reflashing replaces `/data`. Previous images and ZIPs remain archived directly
in `image_files`.

Image SHA-256:
`e43caed4626981486a09b8172a99476bb57dd6fd470bdb0ca9139109fc1131e4`.

Kernel: 421604 bytes, reserved end `0x189000` below firmware DTB `0x200000`.
There are 34 sample/test ELFs. SHELL is 4456 bytes and uses two RX image pages;
PIPECHK is 3496 bytes. Other samples still fit the 4096-byte writable-file bound.

Both final QEMU Cortex-A53/EL1 and Cortex-A76/EL2 pipeline create/reopen tests
pass: 14 self-test pipelines/29 children, blocked reads/writes, independent host
binary digests, EOF/EPIPE, faults, Ctrl+C, enclosing-session expiry, file output
and fresh-boot reads. Independent bank decoding verifies names, lengths, exact
bytes, generations and SHA-256; boot/superblock/unused sectors and the delivery
image remain unchanged. The final child/early-input regression also passes.
The previous file-stream and application/tools suites passed both profiles in
this phase; native file failed-commit/remount/cleanup checks pass. All 34 ELF
fixtures, 25000 parser mutations, 10000 FIFO cycles and disk/ZIP checks pass.

Logs: `build/phase-pipe-{build,image,native,elf,files,qemu,stdio,regression,child-regression}.log`.
New physical pipe acceptance is pending. Test writes target only regular scratch
files or in-memory copies. No physical device was flashed or Git push performed.
