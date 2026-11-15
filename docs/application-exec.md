# Application image replacement and shell program lookup

The [new Pi capture](../proofs/pi5-exec-lookup-2026-11-15.txt) now verifies
HELLO lookup and shell exec to ECHO, code 0, execs=1 and resource restoration.
Broader EXECCHK/pipe/handle behavior retains emulator evidence. The current
phase is [private environments and configurable lookup](application-environment.md).

This phase added `EXEC=31`: a running EL0 application can replace its image
with another validated static AArch64 ELF. Success starts at the new entry
point with a fresh zeroed stack and never returns to the old code. Failure
before publication returns to the original application. It does not create
another process or require an unused process slot.

The Pi's [latest file-command run](../proofs/pi5-file-publication-2026-11-15.txt)
completed with resource restoration. Its `replace` command targeted `not.txt`,
while the subsequent read queried `note.txt`, which correctly kept its old
contents. That capture does not establish overwrite or executable-update
acceptance. Earlier completed hardware checks are accepted; this new phase
is validated locally before delivery. The narrower physical exec result is
recorded above; broader physical acceptance remains pending.

## Using applications

After the established storage/MMU/timer setup, enter `appshell`. At `user:/$`:

```text
path
hello
run echo "hello from another application"
exec echo shell-replaced
```

`path` prints `/data/bin:/boot`. `hello` launches the boot HELLO program as a
child and returns to the shell. `exec echo shell-replaced` replaces the shell
itself with ECHO. After ECHO finishes, the session returns to the monitor.

Bare program names search `/data/bin` first, then `/boot`; each directory tries
the exact name followed by `.elf` if the input name has no extension. `/boot`
uses its FAT case rules; `/data` remains case sensitive. The current directory
is not searched implicitly. A name containing `/` is used directly, including
relative paths such as `./demo.elf`. Arguments and empty quoted words retain
their original contents. Builtins take precedence over searched programs.

Lookup also works with `run`, `runio`, `pipe`, `pipeio` and `exec`. The first
regular-file candidate is selected; an invalid ELF there is refused rather
than silently falling back to a program in the next directory. A missing bare
program sets shell status 127. `exec` failure leaves the current shell running.
This exec-stage release used a fixed path. The subsequent
[environment phase](application-environment.md) now makes PATH configurable.

On a v3 volume, install a persistent program from the shell using unused paths:

```text
mkdir /data/bin
cp /boot/ECHO.ELF /data/bin/speak
speak "stored application"
```

`mkdir` will report an error if that directory already exists; existing files
are never replaced by `cp`. After a later boot and the established setup,
`speak` can be looked up again. Installation does not make `/home` persistent;
the root filesystem remains RAMFS.

## Exec contract

`uos_exec(const struct app_args *)` takes the same checked packed argv as
spawn. The filename is argument zero. On success it preserves:

- PID and file-owner identity, parent relationship and working directory;
- owned file handles and their cursors, including redirected stdin/stdout;
- membership in the existing pipeline and its FIFO streams;
- the original session/child deadline and prior I/O-failure state.

Ordinary handles remain open; close-on-exec flags are not implemented. Image
and stack pages are replaced, registers are reset and old private pages are
zeroed/freed. `execs=` in the process report counts successful image changes.
Arguments are copied before replacing any user memory. Invalid pointer ranges
return EFAULT; malformed argv, invalid/missing ELF, load or allocation failure
returns an error. A process with an outstanding unreaped child receives EBUSY
and must collect that child first. Existing process-tree limits are unchanged.

The kernel prepares an inactive candidate with fresh pages and permission
tables linked for the destination process slot. The old mappings/frame/handles
stay intact during loading. Publication occurs under the canonical kernel
root with IRQs masked, followed by instruction/TLB synchronization before
returning to EL0. The candidate uses separate staging storage, so all three
slots can be occupied. An internal allocator teardown failure is contained
as a process fault; the kernel does not return to freed code.

`EXEC.ELF <ELF path> [args]` is a small chain-loader for use inside jobs. For
example, from the shell:

```text
pipe exec /boot/STREAM.ELF -- exec /boot/HASH.ELF
```

Both children replace themselves while retaining their pipe endpoints. This
uses the existing three slots: shell, producer and consumer. File redirection
also survives exec. These are bounded foreground jobs, not background services.

## Verification

`EXECCHK.ELF` checks invalid ranges/argv/ELF/path refusal without damaging the
old stack, cwd, PID or open file. It then replaces itself eight times, checking
PID stability, file-cursor continuity, quoted/empty arguments and fresh stacks.
Its `parent` mode checks wait on the original child ID, slot reuse, a fault
after exec, and a one-second deadline across repeated image changes.

The new physical self-test, when desired after normal setup, is just:

```text
appterm 120 /boot/EXECCHK.ELF
```

Local verification is `scripts/test-shell-qemu.ps1 -Exec`, using regular scratch
disks. It covers A53/EL1 and A76/EL2, three occupied process slots, repeated
image replacement, owned resources, parent waits, deadlines, redirected binary
I/O, both pipeline peers executing new images, fixed search precedence and
stored-program lookup/execution after a fresh emulator boot. Existing PIPECHK
and SPAWNCHK are included as process/IPC regression checks. Native user-MMU
tests independently verify staged table links and inherited permissions.
Logs are `build/phase-exec-*.log`.

All of the above emulator and native checks passed for this delivery. All 41
delivered ELFs and 25000 deterministic ELF parser mutation cases pass, as do
eight independent image corruption/profile checks. Source-image SHA-256,
boot sectors and sectors outside the data banks remain unchanged in tests.
Process reports finish with owned handles zero and memory resources restored.

The disk image is 134217728 bytes; the verified full-image ZIP is 868841 bytes.
Image SHA-256:
`7be53179792bb065483c58233eb7c9f78fd8cba022f3ec893bdd1cab5060b3b5`.
Kernel image: 433908 bytes, reserved end `0x1c9000` below firmware DTB
`0x200000`. SHELL.ELF: 6552 bytes; EXEC.ELF: 832; EXECCHK.ELF: 3608.

The verified full image and ZIP stay flat in `image_files`, preserving earlier
deliveries. No physical media is written by the build or automated tests.
General process trees, background jobs, dynamic user memory, packages and
local display/input drivers remain later phases.
