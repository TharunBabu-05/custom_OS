# Interactive EL0 serial shell

The current shell supports two background jobs, command lists and `source`.
For commands and installation limits, see [Phase 4 shell workflows](shell-workflows.md).

The v4 storage phase added `df` and larger persistent files. The original
single-job release fit the 8 KiB v2/v3 limit. The Phase 4 shell runs from
`/boot` and fits under `/data` on v4. [v4 storage](data-v4.md) · [Original job release](shell-jobs.md)

**Update:** [Physical shell acceptance passes](../proofs/pi5-user-shell-2026-11-13.txt).
The current [session-profile phase](session-profiles.md) adds `$NAME`/`${NAME}`/
`$?` expansion, saved data-only settings, automatic loading and `--noprofile`.
The current [environment phase](application-environment.md) adds env/getenv/
setenv/unsetenv, configurable PATH, HOME for bare cd and private inheritance.
The preceding [application exec/lookup phase](application-exec.md) adds bare
program names and `exec` to replace the shell image; PATH is now configurable.
The current shell also adds [`replace <source> <target>`](atomic-file-updates.md)
for explicit atomic file publication; `mv` still refuses overwrite.
The current image adds [two-stage pipes](pipelines.md), after
the [file standard input/output](stdio.md),
the [foreground program launch/wait](shell-programs.md) and
the [binary copy, SHA-256 and sync](file-tools.md) stage. The image
hash and sizes below describe the earlier shell release.

The [latest Pi capture](../proofs/pi5-user-tools-2026-11-13.txt) passes application
isolation/preemption tests, argument/cwd/metadata checks, sleep/wake/idle resume,
EL0 LS/CAT/ECHO/SLEEP and final handle/memory checks. The shell below subsequently
passed its own hardware test, linked above. Physical persistence across a power
cycle remains a separate acceptance item.

## Start and use it

After flashing the current complete image, enter these commands at the usual
kernel monitor, one at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
apprun /boot/MUTATE.ELF /boot/TTYDENY.ELF
appshell
```

Both test programs should exit zero with PASS and restored resources.
`appshell` loads `/boot/SHELL.ELF` into a private EL0 process. The prompt becomes
`user:/$`. Now try:

```text
help
pwd
ls /boot
mkdir /tmp/demo
cd /tmp/demo
write note.txt "hello from the user shell"
cat note.txt
write note.txt "short"
cat note.txt
ls
cd /
rm /tmp/demo/note.txt
rmdir /tmp/demo
write /data/shell.txt "saved from EL0"
cat /data/shell.txt
exit
```

`write` creates or **replaces the complete contents** of a file, including
shorter or empty text. It does not append a newline. Use quotes for spaces.
`rm` removes a file; `rmdir` removes an empty directory. `/boot` stays read-only;
`/tmp` stays volatile; `/data` requires `datainit` and keeps its existing eight
flat files/4096 bytes per file limit. Directories cannot be created in `/data`.
File leases still apply, so a conflicting open handle makes mutation fail.

After `exit`, the kernel monitor's original working directory is unchanged:

```text
pwd
appinfo
fileinfo
heaptest
pagetest
timerstop
```

Expect `state=exited code=0`, `cleanup=restored`, zero open handles and memory
checks passing. RAM files left by the shell are intentionally filesystem-owned;
they are not a process memory leak.

To verify persistence on Pi, power-cycle **without reflashing** after the saved
file is written. Run `storageinit`, `datainit`, `mmuinit`, `irqinit`, `timerstart`,
`appshell`, then `cat /data/shell.txt`. Capture the new boot banner and output.
Reflashing replaces `/data` and is not this persistence test.

## Terminal behavior and limits

- Backspace/Delete removes one character; Ctrl+U clears the line.
- Ctrl+D on an empty line exits the shell. On a nonempty line it is ignored.
- Ctrl+C at the boot-shell or `appshell` prompt clears the line. With a
  foreground child running it cancels that child with code 130. For a
  standalone `appterm` program it cancels the terminal application.
  Synchronous kernel I/O can delay cancellation.
- Input is printable ASCII. There is no arrow-key/history/ANSI editor or hidden
  password input in this shell. Input is echoed; do not use it for credentials.
- Lines contain at most 127 characters. Overflow rejects the whole line on
  Enter without executing it. Ctrl+U can clear an overflowing line. Quoting
  and up to eight words use the same bounded parser as `appexec`.
- Only one foreground terminal process owns input. The kernel monitor is
  suspended during the session. Noninteractive `apprun`/`appexec` programs
  cannot consume serial input.
- The boot shell and `appshell` stay open until exit. Child processes and
  pipelines retain their per-launch deadlines. If `exec` replaces an
  open-ended shell, the replacement gets a 30-second deadline. Subsequent
  replacements inherit that deadline. Ordinary application batches have a
  two-second budget.
- `appterm <1..300 seconds> <ELF> [arguments]` assigns the terminal to another
  single application, or runs a shell with a chosen deadline. For example:
  `appterm 60 /boot/SHELL.ELF`. All launch prerequisites still apply.

The `scripts/test-shell-qemu.ps1 -LongShell` regression checks prompt
interruption, background-job retention and foreground cancellation on both
CPU profiles. It advances the virtual clock past the old deadline and checks
that `appterm` and an executable that replaces the shell still time out.

UART remains polled without flow control. Input is serviced in bounded batches
on timer/idle scheduling points. A 128-byte kernel queue preserves input while
the application computes; if that queue overflows, input is discarded through
the next newline and the application is cancelled with code 75. Ctrl+C also
escapes that state. Buffered input is wiped at process cleanup. Wait for each
prompt when sending commands; arbitrary high-speed paste is not guaranteed.

## What is implemented

SHELL is an original 2968-byte static executable, running at EL0 with the same
private mapping and fault containment as other applications. Its parser and
command logic run in user mode and use SVC for terminal/filesystem access; it
never calls the kernel monitor's command dispatcher. Builtins are `help`,
`pwd`, `cd`, `ls`, `cat`, `echo`, `write`, `mkdir`, `rm`, `rmdir`, and `exit`.
This earlier release had no child ELF launch. Current images add bounded
[foreground run/wait](shell-programs.md), [file redirection](stdio.md),
[pipelines](pipelines.md) and [private environments](application-environment.md).
General job control, persistent profiles, a permission model and POSIX
compatibility remain outstanding.

New ABI calls preserve existing numbers 0..16:

| Call | Arguments and result |
| --- | --- |
| READLINE (17) | output pointer, capacity 2..128; block until line; return length excluding NUL or negative error |
| MKDIR (18) | path pointer, length excluding NUL; return zero on success |
| REMOVE (19) | path pointer, length, directory flag 0/1; return zero on success |
| REPLACE (20) | path pointer, length, data pointer, data length <=512; return bytes written |

READLINE checks the entire output capacity for writable private mappings before
accepting a request. It copies a NUL-terminated line only after successful
completion. Overflow/EOF errors leave the user's buffer untouched. Error codes
are -1 for generic failure, -2 for invalid memory, -3 for EOF, and -4 for no
terminal assignment. Ctrl+C at the open-ended shell prompt completes the
call with an empty line. EOF is a distinct result.

INPUT_WAIT is a separate scheduler state. No blocking UART loop runs inside an
exception handler. The all-blocked path restores the kernel root, leaves the
dispatcher, unmasks IRQs and waits for timer interrupts. Line completion makes
the saved process frame runnable with its syscall result. Timeout, cancellation,
normal exit and faults all use the existing handle/page cleanup.

Mutation calls validate lengths and all input mappings before VFS access,
resolve paths against private cwd, and use public VFS APIs so mount bounds and
leases remain enforced. Directory removal refuses ancestors of any live
process cwd or the monitor cwd. REPLACE uses one bounded VFS replacement rather
than separate truncate/write commits. The existing snapshot failure policy
still applies: ambiguous storage failure requires reboot, not blind retries.

TTYCHK tests pointer/capacity errors and blocking input while holding a file;
TTYDENY verifies terminal denial outside an interactive session; MUTATE verifies
short/empty replacements, mutation bounds, leases, boot read-only behavior and
cwd protection, cleaning its `/tmp/.user-fs` fixture. There are now 23 ELF files.

## Verification and delivery

`scripts/test-terminal.ps1` exercises line editing, overflow, EOF/cancellation,
completed-state immutability and 20,000 deterministic input traces. ELF tests
validate all 23 programs. Storage and file-handle native regressions retain
failure injection, lease and independent persistent remount checks.

`scripts/test-shell-qemu.ps1` tests A53/EL1 and A76/EL2 against regular scratch
disk files: interactive file operations, quoting/editing/overflow, bad buffers,
terminal ownership, cancellation during input and a spinning loop, input-queue
overflow, blocked-input timeout, EOF, cleanup and a fresh-boot saved-file read.
It independently checks both snapshot hashes/contents and unchanged boot,
superblock, unused sectors and source image. `test-tools-qemu.ps1` retains the
earlier application/protection/preemption/sleep/installation regressions.
Both emulator profiles and all listed native/storage/image checks pass; logs
are `build/phase-shell-*.log`. Physical shell acceptance subsequently passed.

Full disk: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip` (836856 bytes), verified
to contain the full image. Kernel is 413412 bytes; reserved end `0x186000`
is below the firmware DTB at `0x200000`. Whole-image SHA256:

```text
8eacaf91f146dde0dadeac95790d94f22dab1bbd52f7fac133bc40c0a959f395
```

Prior complete images/ZIPs remain archived. No physical media is flashed and
no Git commit or push is made. Larger persistent storage/recovery, process
spawn/wait/IPC, packages, background networking and local hardware/UI remain
in [remaining-os-roadmap.md](remaining-os-roadmap.md).
