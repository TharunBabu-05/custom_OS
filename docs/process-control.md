# Process snapshots, child polling and cancellation

This phase adds three checked system calls and `PS.ELF`. Programs can list
the current batch, inspect their own child without blocking and cancel that
child without taking control of unrelated processes. The existing three-slot,
single-CPU scheduler and foreground session limits remain in force.

The preceding [Pi allocator capture](../proofs/pi5-user-allocator-2026-11-15.txt)
passes HEAPCHK/HEAPDEMO with code 0, zero live pages and restored resources.
That accepted result is retained. New process-control hardware acceptance is
pending; the tests below exercise only the new phase.

## Install the update without reflashing

The [boot-update ZIP](../build/rpi5-header/OurOS-process-control-boot-update.zip)
contains **OUROS.IMG and PS.ELF**, a manifest, hashes and instructions. It is
for the preceding Pi 5 header-console memory/allocator installation. The new
program needs the new kernel; copying PS.ELF alone is insufficient.

With the Pi powered off, connect its pendrive to Windows and extract the ZIP
on the PC. On the existing boot volume, check the current OUROS.IMG hash with
`Get-FileHash X:\OUROS.IMG -Algorithm SHA256` (use its actual drive letter).
It must match the manifest's expected previous kernel:
`72852aa77b033b9f6330b0f93a8f9dbcbff8d9c864568db792d51d3946813065`.
Keep a PC copy of that old kernel, then copy the two new payload files beside
CONFIG.TXT and PI5.DTB, replacing OUROS.IMG. Wait for copying to finish and
safely eject. Firmware, configuration, DTB and `/data` are unchanged.

This is a boot-file update, not a disk image or an on-device atomic updater.
The compatibility check is scoped to the preceding installation, not arbitrary
images. The full [IMG](../image_files/OurOS-Pi5-header-experimental-sd.img) and
[full-image ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip) remain
available for a fresh installation; flashing those replaces `/data`.

## Try it on the Pi

After a fresh boot, enter at the kernel `>` prompt:

```text
storageinit
mmuinit
irqinit
timerstart
appterm 120 /boot/PS.ELF check
```

Expect two PASS lines, process code 0, `heap_live_pages=0` and
`cleanup=restored`. The check creates a temporary RAM file for lease tests and
removes it; it does not write `/data`. `datainit` is optional for these tests.

Then run this from the kernel monitor:

```text
appterm 120 /boot/PS.ELF demo
```

At the new `process>` prompt, enter one command per line:

```text
status
stop
start
status
stop
exit
```

The parent remains interactive while its quiet child holds two private pages
and a read handle. `status` shows both processes and polls the child. `stop`
cancels and collects it, normally printing `Child collected: exit=130`.
`start` reuses a free slot with a new PID. `wait` collects naturally completed
children or waits for their existing deadline. Completed outcomes are retained:
stopping an already timed-out child returns 124, rather than rewriting it to 130.
Exiting or cancelling the parent also reclaims its outstanding child.

Each demo child has a 30-second deadline; the parent has the chosen terminal
session budget. The demo is one bounded job, not general shell background-job
syntax. Run check/demo directly with `appterm`: a shell-launched child is not
allowed to spawn another generation of processes.

To view processes from the normal user shell:

```text
appshell
ps
exit
```

`ps` is an EL0 executable, not a kernel-monitor command. With the shell running
it normally shows the shell and viewer. A parent can be `ready` if the timer
dispatched the child before the parent entered wait, or `wait-child` afterward.
The snapshot is coherent at the instant of the call; states can change before
the text finishes printing. Kernel cooperative tasks are not included.
The PAGES column counts dynamic heap pages only; executable and stack pages
are separate and still consume RAM when that column is zero.

## API and ownership contract

| Call | Interface | Result |
| --- | --- | --- |
| 40 PROCESSES | `uos_processes(struct uos_process_table *)` | One coherent 952-byte, version-1 snapshot of up to three entries |
| 41 CHILD_POLL | `uos_child_poll(pid, struct uos_child_info *)` | A 24-byte pending/completed child result, without sleeping or reaping |
| 42 CHILD_CANCEL | `uos_child_cancel(pid)` | Mark a live owned child cancelled with code 130; preserve an already completed outcome |

Earlier call numbers are unchanged. This is the original OurOS ABI, not Linux
or POSIX syscall compatibility. `UOS_ECHILD=-14` means there is no matching
unreaped direct-child relationship. Zero, self, parent, sibling, unrelated and
already-reaped PIDs cannot grant cancellation authority. Pipeline peers also
cannot control each other or their parent. PIDs do not repeat during a boot;
they are not persistent identifiers across reboot.

Each snapshot entry contains PID/parent PID, public state, caller/owned-child
flags, syscall and dispatch counts, live heap pages, completed exit code and
a terminated executable path. Root processes have parent PID zero. Unused
entries, reserved fields and bytes after the path terminator are zero. Kernel
addresses, arguments, cwd, environment and credentials are not exported. The
current account model has no per-user visibility policy; public batch metadata
is shared, while modification remains limited to the direct parent relation.

States distinguish running, ready, sleeping, terminal input, child wait, pipe
read/write wait, exited, faulted, timed out and cancelled. Only the caller is
marked running in its snapshot. A poll returns `finished=0` and exit code zero
for a pending child, or `finished=1` with the retained terminal state/code.

The snapshot requires the exact structure size. Its whole destination is
validated writable before copying; bad size returns UOS_ERROR and invalid,
read-only, freed or guard-crossing memory returns UOS_EFAULT without a partial
write. Poll validates ownership before checking its output pointer and likewise
checks the entire destination. New syscall work runs under the canonical kernel
root with IRQs masked, as in the existing single-CPU syscall path.

Neither poll nor cancel collects a child. Cancellation is idempotent until
collection, and a finished result remains pollable. Ordinary `uos_wait` reaps
it once; subsequent poll/cancel returns UOS_ECHILD. A completed child can retain
its slot, pages and file leases until wait or parent teardown. Programs must
collect children when finished. Demo `stop` performs both operations.

This phase does not add arbitrary process killing, signals, process trees,
multiple outstanding ordinary children per parent, shell `&`/jobs syntax,
indefinite services, an IRQ-preemptible kernel or SMP.

## Validation and artifacts

`PS.ELF check` tests zero-filled snapshot bounds/identity, bad sizes/pointers,
freed and guard-crossing outputs with no partial write, pending polling, repeated
cancellation, stale IDs, refusal to reuse an unreaped slot and release of leases
after wait. It also checks preserved normal/fault/exec/timeout outcomes and a
three-slot pipeline whose peers reject control of their parent and sibling.

`test-shell-qemu.ps1 -ProcessControl` adds three simultaneous viewers, cancelled
pending terminal reads, later parent/child terminal handoff, interactive demo
reuse, parent exit/Ctrl+C/timeout cleanup, streams, persistent output and an
installed viewer after a fresh emulator boot. The stored snapshot text is
compared across boots. Handles and private memory must restore; source delivery
and boot/unused sectors must remain unchanged. It runs A53/EL1 and A76/EL2 on
regular scratch disk files, never on the physical pendrive. Logs use
`build/phase-proc-*.log` and `build/handoff-test/proc-*.log`.

The boot-update packager checks both payloads against the full-image manifest,
the exact ZIP entry set and every extracted hash. The full IMG and verified
full-image ZIP are archived flat in `image_files`; earlier deliveries remain.
The current PS.ELF is 7776 bytes and fits the v2/v3 8192-byte installation limit.
The kernel remains 446196 bytes but its hash changes for the new API; the prior
allocator-only bundle is not the process-control update.

The new process-control suite passes on A53/EL1 and A76/EL2, as do all 50 ELF
loader checks, 25000 parser mutations and eight image corruption/profile
rejection cases. These are local checks; new Pi acceptance remains pending.
The original application/tools suite also passes on both profiles, including
checked syscalls, private mappings, preemption, contained faults/timeouts and
installed application execution after a fresh emulator boot.

Full-image bytes: 134217728; full-image ZIP: 891092; boot-update ZIP: 212904.
Kernel reserved end stays `0x1d0000`, below the firmware DTB at `0x200000`.
Full-image SHA-256:
`656b6f0d0889b6cbe6833e485debab1acb0829d4a9276a06a84ea802ea6aea5f`.
New kernel SHA-256:
`af25f87a94a7f188c16255fc5c2c01eb2e074f49af381da3f03c4487d308e8de`.
Boot-update ZIP SHA-256:
`909406b380622fba5c7462c7d1974d305eea8409183938824ae19ac4dd341392`.
