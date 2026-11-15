# Bounded background jobs in the user shell

The normal EL0 shell now supports one background child while it remains
interactive. It uses the existing process-control calls; the kernel binary,
syscall numbers, three process slots and v1/v2/v3 data formats are unchanged.

The [new Pi capture](../proofs/pi5-process-control-2026-11-15.txt) passes the
preceding PS check (12 children started/reaped, code 0 and resources restored)
and the interactive stop/start demo (two children collected, no remaining heap
pages and resources restored). This accepts those process-control paths on Pi.
The [subsequent Pi job capture](../proofs/pi5-shell-jobs-2026-11-15.txt) now
verifies bg/jobs/echo/stop/exit: SPIN runs while the shell remains responsive,
stop returns 130, one child is started/reaped, the job record clears, and shell
exit reports code 0, no live heap pages and restored resources. Five user
preemptions are reported. This accepts that path on Pi. fg/bgio, natural
deadlines, parent cancellation and installed-shell reboot remain separate
hardware cases; their emulator checks already pass.

## Application-only update

Use [OurOS-shell-jobs-update.zip](../build/apps/OurOS-shell-jobs-update.zip).
It contains SHELL.ELF, instructions, a manifest and SHA-256 values. For the
preceding Pi-tested process-control installation, copy only the new SHELL.ELF
onto its existing FAT boot volume, beside OUROS.IMG, replacing the old shell.
Power the Pi off first, keep a PC backup of the old shell and safely eject the
pendrive after copying. The kernel, firmware, configuration and `/data` stay
in place. This ZIP is an application update, not a disk image.

The included instructions check that the existing OUROS.IMG SHA-256 is:
`af25f87a94a7f188c16255fc5c2c01eb2e074f49af381da3f03c4487d308e8de`.
This phase rebuild produces that same kernel hash. The old PS and other user
programs remain compatible with the unchanged API; they need not be copied
for this application-only update. A shell already installed under `/data`
is a separate file. Update that copy explicitly with the established
`appupdate /boot/SHELL.ELF /data/bin/shell.elf` workflow if desired.

## Try a background job

At the kernel monitor after boot:

```text
storageinit
mmuinit
irqinit
timerstart
appshell
```

At `user:/$`, enter one line at a time:

```text
bg 30 /boot/SPIN.ELF
jobs
echo shell-is-responsive
stop
jobs
exit
```

`bg` prints the child's PID and returns to the shell prompt. `jobs` reports it
as pending or done. `stop` cancels and collects it; before its deadline this
normally prints `child exit=130`. The final `jobs` reports no background job,
and shell exit returns to the monitor with restored resources. A completed
outcome is preserved: if the 30-second deadline already expired, collection
returns 124 rather than rewriting it to 130.

For terminal handoff, start a reader in the background and then bring it forward:

```text
bg 30 /boot/TTYCHK.ELF
jobs
fg
terminal-ok
```

The reader cannot take shell input before `fg`. `fg` waits for the child and
hands it the terminal. After the reader exits, the shell prompt returns.
Ctrl+C while a child is foreground cancels that child; Ctrl+C at the shell
prompt cancels the shell session and its outstanding child.

## Commands and limits

| Command | Behavior |
| --- | --- |
| `bg SECONDS PROGRAM [args]` | Resolve the executable through PATH or an explicit path, start it and keep the shell interactive |
| `bgio SECONDS IN\|- OUT\|- PROGRAM [args]` | Start with binary file stdin/stdout; `-` keeps the default stream |
| `jobs` | Poll without blocking or collecting; show PID and pending/done outcome |
| `fg` | Wait/collect the outstanding job, with terminal handoff when needed |
| `stop` | Cancel then wait/collect; preserve an already completed result |

SECONDS must be an integer from 1 through 30. Waiting or foregrounding never
restarts that deadline. The shell retains its terminal session budget, normally
300 seconds from `appshell`. Pending includes runnable, sleeping or input-wait
states; use the process API for detailed states.

There is one outstanding job record, including an unreaped completed child.
Collect it with `fg` or `stop` before another external launch, exec or pipeline.
The shell refuses those operations before attempting new streams or replacing
itself. Filesystem, echo, environment and other builtins remain usable. Job
records are private to that shell session and are not saved in profiles.
The foreground result supplies `$?`, just like existing run/wait operations.
`jobs` reports an outcome without making it the collected foreground status.

Default background output shares the terminal and may interleave with a prompt;
use `bgio` to capture it. Background terminal reads wait for explicit `fg`.
The kernel still completes syscalls/drivers synchronously; this phase does not
make kernel I/O preemptible or implement an indefinite background network service.
No `&` syntax, multiple jobs, process trees or signals are added.

After `datainit`, a persistent output example is:

```text
bgio 30 - /data/job.txt /boot/ECHO.ELF hello-background
fg
cat /data/job.txt
```

The output path must be new. Existing output files are refused; input/output
handles are owned by the child and close through the existing exit, collection
and parent-teardown paths. Its cwd and environment are snapshots taken at launch. Parent exit,
Ctrl+C or session timeout reclaims an outstanding child using the existing
kernel lifetime rules. The root filesystem remains volatile RAMFS; `/data`
is the persistent mount.

## Implementation and verification

SHELL.ELF is 8162 bytes and remains installable on v2/v3 volumes under the
unchanged 8192-byte file bound. The command dispatcher uses a compact packed
name table and shared console output, and the shell build omits frame pointers.
The integer ABI and guarded stack are unchanged. No writable ELF globals are
introduced; job state lives in the shell stack.

Named runtime calls use two- or four-argument SVC wrappers instead of loading
zeroes into unused argument registers. The kernel preserves the registers that
a call does not use. The full six-argument `uos_call` remains available for raw
tests/future calls; no kernel ABI or call-number change is involved. Rebuilt
sample/test ELFs exercise the compact wrappers; previous binaries still work.

`test-shell-qemu.ps1 -ShellJobs` passes A53/EL1 and A76/EL2 acceptance:
nonblocking parent builtins, malformed/deadline bounds, one-job/launch guards,
lease retention/release, normal/fault/exec/timeout results, pending child input,
fg/Ctrl+C, parent exit/cancellation/timeout cleanup, cwd/environment inheritance,
file output/hash and installed-shell execution after a fresh emulator boot.
Handles and private pages restore, and source/boot/unused sectors stay unchanged.
Only regular scratch disk files are written by the automated tests.

The packager verifies the pinned kernel, shell size/full-image manifest match,
exact ZIP entry set and every extracted hash. Full 128 MiB images and verified
full-image ZIPs remain flat in `image_files`, with earlier phases preserved.
Full-image flashing replaces `/data`; the shell-only update avoids that here.
Logs use `build/phase-jobs-*.log` and `build/handoff-test/jobs-*.log`.

The profile/expansion suite (including legacy v1/v2 mounts), process-control
suite and original application/tools suite also pass on both emulator profiles.
All 50 actual ELFs, 25000 loader mutations and eight image corruption/profile
rejection cases pass. The old application fixture now derives HELLO's length
from the built file rather than requiring a former 528-byte encoding; installed
bytes and execution/resource checks remain independent.

Delivery sizes: SHELL.ELF 8162 bytes; shell-only ZIP 6932; full-image ZIP 891132.
The full IMG is 134217728 bytes. Kernel SHA-256 remains the pinned value above;
its size stays 446196 bytes and reserved end `0x1d0000`.
Full-image SHA-256:
`07e9f878750a7d541d8159e5e24c7845d9a843297715071464d5b2408585879c`.
Shell-update ZIP SHA-256:
`ff97ba3d0b69f3312fd547b0a09ea3a8f64112d495f7374aa58143f7b54cec27`.
