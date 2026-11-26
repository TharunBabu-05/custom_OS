# Process snapshots, child polling and cancellation

[Phase 33 process trees](#phase-33-process-trees),
[Phase 34 EL0 scheduling](#phase-34-el0-scheduling), and
[Phase 35 process capacity](#phase-35-process-capacity) extend the
original process-control release described below.

This phase adds three checked system calls and `PS.ELF`. Programs can list
the current batch, inspect their own child without blocking and cancel that
child without taking control of unrelated processes. The existing three-slot,
single-CPU scheduler and foreground session limits remain in force.

The preceding [Pi allocator capture](../proofs/pi5-user-allocator-2026-11-15.txt)
passes HEAPCHK/HEAPDEMO with code 0, zero live pages and restored resources.
That accepted result is retained. The later
[Pi process-control capture](../proofs/pi5-process-control-2026-11-15.txt) now
passes PS check/demo, all 12 check children reaped and two demo stop/start
cycles with restored resources. This accepts those shown paths; broader
installed-viewer reboot and explicit terminal-check hardware cases remain open.
The next [shell-job phase](shell-jobs.md) reuses this kernel and replaces only
SHELL.ELF for a scoped application update.

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

Each demo child has a 30-second deadline. The parent has the chosen terminal
session budget. Run the original check/demo with `appterm` to match its
three-slot test setup.

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
| 40 PROCESSES | `uos_processes_v2(struct uos_process_table_v2 *)` | One coherent 1576-byte, version-2 snapshot of up to five entries. The kernel selects the schema by buffer length. |
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

The artifact figures below describe the original process-control release;
current delivery figures are in [shell-jobs.md](shell-jobs.md).

The new process-control suite passes on A53/EL1 and A76/EL2, as do all 50 ELF
loader checks, 25000 parser mutations and eight image corruption/profile
rejection cases. PS check/demo now also pass on Pi, within the scope recorded
above; additional physical terminal/reboot cases remain open.
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

## Phase 33 Process trees

Ordinary EL0 applications can spawn children in the five-slot foreground
table. Each parent can have two outstanding ordinary children. `ps` shows each
process's immediate parent. Wait, cancellation, signals and shared-memory
grants are scoped to direct children. IPC works between adjacent levels of the
tree. When a process stops, the kernel cancels any live descendants and
releases their pages and handles before recycling their slots. The original
parent exit code remains available to its direct parent through `wait`.

The serial terminal follows the shell's chain of foreground waits. A reader
can prepare a read early and receives input after each parent in the chain
waits for its child. An input file ends serial input delegation for that
branch. Ctrl+C stops the process that currently owns the terminal.

### Try the Pi 5 update

In Windows PowerShell, with the Pi powered off and its normal boot drive
mounted as `D:\`, preview and apply the boot update. Substitute the drive
letter if Windows assigns a different one.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase33-process-trees-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, enter each command and
wait for its output before continuing.

```text
run /boot/TREECHK.ELF
runio /etc/hostname - /boot/TREECHK.ELF input-file
run /boot/TTYPAR.ELF
```

Expect PASS for process ownership and cleanup and for redirected input. When
the terminal test prints `Parent now waits for the early terminal reader.`,
type `terminal-ok` and press Enter. Expect a terminal handoff PASS and
`child exit=0`. Run `run /boot/TTYPAR.ELF` once more and press Ctrl+C after the
same prompt. Expect `^C`, `child exit=2` from the test wrapper, and a shell
prompt.

```text
echo nested-terminal-restored
exit
fileinfo
storagetest
datacheck
```

Expect matching child start/reap counts, restored resources, zero open files,
a storage read PASS, and a valid mounted generation. The same path runs on
the emulator through `scripts/test-shell-qemu.ps1 -ProcessTrees` with a
v5 test image that contains the matching kernel and apps.

Both QEMU profiles passed the nested wait, exit, cancellation, fault and
timeout tests. Each run reaped all 48 children, restored resources and ended
with zero open files and a matching v5 generation. The Phase 30 Core OS and
four-stage pipeline suites passed on both profiles. Boot-update preview,
apply, readback and rollback passed against a Windows scratch directory.
The Pi passed the tree and redirection tests at v5 generation 41. `TTYPAR`
reached the prompt and returned exit 2. The manual test below waits longer
and reports the reader failure. [Pi capture](../proofs/pi5-process-trees-2026-11-25.txt)

## Phase 34 EL0 scheduling

The scheduler rotates through runnable EL0 slots. Low priority can use one
timer tick per turn, normal two, and high four. Blocking and `yield` end the
current turn early. At a steady 100 Hz these are nominal 10, 20, and 40 ms
turns for continuously runnable apps. New processes
start at normal priority or inherit their parent's priority. `exec` keeps
the existing process's priority and tick counts.

`sched` reports the shell's priority. `sched PID` reports a direct child's
priority and counters until the shell collects it. `sched PID high` changes
the priority of a live child. Use `low`, `normal`, or `high`. The two syscall
IDs are 77 (`uos_sched_get`) and 78 (`uos_sched_set`). A GET with PID zero
reports the caller. `user_ticks` counts timer samples taken while the process
was running in EL0. `ready_ticks` counts EL0 timer samples when it was ready
and another process was running. These counters measure timer samples.

### Try the Phase 34 update on Pi

With the Pi powered off and the normal boot volume mounted as `D:\`, run
these commands from the project in Windows PowerShell. Use the drive letter
Windows assigns to the boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase34-scheduling-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, run each command and
wait for its output before starting the next.

```text
sched
run /boot/SCHEDCHK.ELF
runio - - /boot/TTYPAR.ELF manual
```

Expect normal priority for the shell. `SCHEDCHK` prints two pairs of tick
counts as it switches the two CPU-bound jobs' priorities, then prints PASS.
When `TTYPAR` prints `Parent now waits for the early terminal reader.`, type
`terminal-ok` and press Enter within 25 seconds. The success run prints a
terminal handoff PASS and `child exit=0`. A missed deadline prints
`TTYPAR: reader timed out.`

Then run `exit`, `fileinfo`, `storagetest`, and `datacheck`. Expect restored
resources, zero open files, a read-only storage PASS, and a valid mounted
generation.

Both QEMU profiles passed `test-shell-qemu.ps1 -Scheduling`, including
priority swaps during a CPU-bound run, checked access, inheritance, and
`exec` accounting. The manual terminal test accepted input after six
seconds. The separate no-input run returned the timeout message before
the shell's deadline. Both profiles also passed the process tree, Core OS,
four-stage pipeline, network worker, and two-job checks. The boot updater
passed preview, apply, readback, and rollback on a Windows fixture.
The Pi measured 30 low-priority ticks and 120 high-priority ticks in both
priority assignments. The manual nested terminal test accepted `terminal-ok`
and both test applications exited with code zero. [Pi capture](../proofs/pi5-scheduling-2026-11-25.txt)

## Phase 35 Process capacity

The foreground batch has eight slots, with a separate slot for the supervised
service. A pipeline can run seven stages alongside its launching shell. The
shell tracks four background jobs and can launch a foreground app while up to
three background jobs are tracked. Collect background jobs before launching
a pipeline.

- `jobs` lists all tracked jobs. `fg PID` and `stop PID` select one job.
- `waitall` collects every tracked job and returns the first nonzero child
  status in job-table order. A waiting job can receive foreground serial input.
- `stopall` cancels live jobs and collects every outcome. It returns success
  when collection succeeds. A job that already finished keeps its exit code.
- `ps -l` displays all active or retained process entries, including an
  active service, with the time slice, timer samples, and parent PID.

`UOS_SYS_PROCESSES` accepts the 3040-byte v3 table as well as the original
v1 and v2 sizes. Each of the nine v3 rows can carry scheduler counters and a
service flag. A shorter snapshot reserves space for its caller. Syscall 79,
`uos_spawn_ex`, allows four outstanding ordinary children and accepts optional
file streams. The original spawn entry points impose their two-child quota.

### Try the Phase 35 update on Pi

With the Pi powered off and its boot partition mounted as `D:\`, run the
update from the project folder. Use the drive letter Windows assigns.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase35-process-capacity-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. Run `source -e /boot/PROCS35.OSH` at `user:/$`.
The script checks a 16 KiB stream through seven stages, displays a full
process table, and exercises `stopall` and `waitall` with four jobs each.
Expect `PASS: seven-stage binary pipeline, process inventory and four-job control.`

Run these lines separately. `exit` switches to the `>` monitor prompt.

```text
exit
cd /boot
apprun CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF CAPCHK.ELF
cd /
fileinfo
storagetest
datacheck
```

Expect eight capacity and snapshot PASS lines, restored process resources,
zero open files, a read-only storage PASS, and a matching data generation.

Both QEMU profiles passed the full table, the 16 KiB seven-stage transfer,
the ninth service row, job limits, group outcomes, and rollback of a failed
pipeline launch. Each run reaped all 31 children and left zero open files.
The Core OS, tree, scheduler, job, descriptor, and four-stage pipeline suites
also passed. The Pi build leaves 12 KiB before the firmware device tree.
The boot updater passed preview, apply, readback, and rollback on a Windows
fixture. The Pi passed the script, displayed eight process rows, and reaped
all 23 children. File and USB checks passed with v5 generation 41 still
mounted. [Pi capture](../proofs/pi5-process-capacity-2026-11-25.txt)

## Phase 37 Named IPC

An EL0 task or supervised service can publish a local listener. Other
tasks can request a connection by name. The listener accepts pending
requests in arrival order. The accepted pair can exchange messages in
both directions, including across process trees. The name accepts one
to fifteen lowercase ASCII letters, digits, dots, underscores or hyphens.

The system holds up to four listeners and eight pending or active
connections. Each process has a four-message inbox shared by its channels
and direct parent-child IPC. Each message carries up to 96 bytes and an
optional ordinary file descriptor with explicit read or write rights.
The kernel supplies the sender PID on receive and sets `CLOEXEC` on the
received descriptor.

| Runtime call | Result |
| --- | --- |
| `uos_port_listen(name)` / 84 | Publishes a unique name and returns an owner-bound listener token. |
| `uos_port_connect(name, pid)` / 85 | Returns a pending channel token. Pass zero to select by name, or the expected owner PID to bind to that process. |
| `uos_port_accept(listener, wait_ms)` / 86 | Accepts the next pending connection and returns its token to the listener owner. |
| `uos_port_close(token)` / 87 | Closes a channel from either side, or closes an owned listener and all its channels. |
| `uos_port_list(out)` / 88 | Lists live names, owner PIDs and counts of pending and active channels. |
| `uos_channel_send(token, message, wait_ms)` / 89 | Sends to the other participant with `message.peer=0`. |
| `uos_channel_recv(token, message, wait_ms)` / 90 | Receives from the selected channel. |

A zero wait returns `UOS_EAGAIN` when the operation would block. Positive
waits up to 10,000 ms use the scheduler timer and can end with
`UOS_ETIMEDOUT` or `UOS_EINTR`. A pending channel can wait for admission
before exchanging data. Close, exit, cancellation and successful exec
end the affected connections, discard their queued messages, release
queued file references, and wake waiting peers with `UOS_EPIPE`. A failed
exec preserves its listener and channels.

### Try the Phase 37 update on Pi

With the Pi powered off and its boot partition mounted as `D:\`, run
from the project folder in Windows PowerShell. Use the actual drive letter.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase37-named-ipc-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject the pendrive and boot the Pi. At `user:/$`, run each line
separately and wait for the next prompt.

```text
run /boot/CHANCHK.ELF
exit
```

The check includes an intentional failed exec of `NONE.ELF`. Expect
`PASS: named listeners, sibling channels, timed queues, descriptor rights and teardown.`
and `child exit=0`.

At the monitor `>` prompt, start a supervised echo server and reopen the shell.

```text
svc start /boot/IPCECHO.ELF serve demo37
appshell
```

At `user:/$`, check discovery and the echo, then return to the monitor.

```text
ports
ipcecho call demo37 pi-ipc
exit
```

Expect a `demo37` row and `IPC echo matched: pi-ipc`. At `>`, call
the same service from a fresh foreground application, then stop it and
check cleanup.

```text
appexec /boot/IPCECHO.ELF call demo37 pi-again
svc stop
appexec /boot/PORTS.ELF
fileinfo
storagetest
datacheck
```

Expect `IPC echo matched: pi-again` before stopping the service. After
shutdown, expect `No listening ports.`, zero open files, a read-only
storage PASS, and `matches_mounted=yes`.

The named IPC suite passed on both QEMU CPU profiles. It covered blocked
accept and send, timeout and signal interruption, descriptor rights, and
cleanup on cancellation, close, exit and exec. The echo service kept the
same PID across two shell sessions. Shutdown left zero file handles and
the test disk hash matched its starting value. The Phase 30 Core OS and
Phase 36 VM suites also passed using the Phase 36 disk. The Windows updater
passed installation and rollback checks. The Pi passed CHANCHK and both
echo calls. Service shutdown left zero listeners and open files, and the
storage checks matched v5 generation 41. [Pi capture](../proofs/pi5-named-ipc-2026-11-26.txt)

## Phase 38 Event waits

`uos_event_wait(entries, count, wait_ms)` watches up to nine sources in
one syscall. Each `struct uos_event` describes a source, an object ID and
requested flags. The syscall fills `revents` and returns the number of
ready rows. A zero timeout samples immediately. A positive timeout waits
up to 10,000 ms and returns zero if the deadline expires.

| Source | Object | Readiness |
| --- | --- | --- |
| `UOS_EVENT_LISTENER` | Owned listener token | `READ` when a connection request can be accepted. |
| `UOS_EVENT_CHANNEL` | Owned channel token | `READ` for a queued message and `WRITE` for free space in the admitted peer's inbox. |
| `UOS_EVENT_MAILBOX` | Parent or child PID, or zero for any direct message | `READ` for a matching direct message. |
| `UOS_EVENT_CHILD` | Direct child PID | `READ` when a stop outcome is ready to collect. |
| `UOS_EVENT_SIGNAL` | Zero | `READ` while `SIGUSR1` is pending. |

Closed and inaccessible sources return `HANGUP`, including when the row
requested only `READ` or `WRITE`. An unadmitted channel becomes readable
or writable after the server accepts it and the corresponding condition
is true. Each event is a snapshot. Use the existing accept, receive, wait
or signal call to act on it. A pending `SIGUSR1` returns `UOS_EINTR` when
the wait set has no signal row.

The scheduler copies the wait set before blocking and validates the output
pages again on wake. Revoking a shared output mapping returns `UOS_EFAULT`.
`ps` shows blocked event waits as `ipc-recv`.

`IPCECHO.ELF` now waits for its listener and all active channels together.
It accepts new clients while established channels are idle or waiting for
inbox space. Each channel has its own three-second idle deadline and reply.

### Try the Phase 38 update on Pi

With the Pi powered off and the boot pendrive mounted as `D:\`, run the
following in Windows PowerShell from the project folder.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase38-event-wait-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. At `user:/$`, run each command separately.
Wait for the event-wait PASS and `child exit=0` before entering `exit`.

```text
run /boot/EVENTCHK.ELF
exit
```

At the monitor `>` prompt, start the echo server and reopen the shell.

```text
svc start /boot/IPCECHO.ELF serve eventdemo
appshell
```

At `user:/$`, run the two-client check. The first client holds its
connection open while a second process connects and exchanges a message.
Expect `PASS: echo multiplexed two independent clients.`

```text
run /boot/EVENTCHK.ELF echo eventdemo
ports
exit
```

At `>`, stop the service and verify cleanup.

```text
svc stop
appexec /boot/PORTS.ELF
fileinfo
storagetest
datacheck
```

Expect `No listening ports.`, zero open files, a read-only storage PASS
and `matches_mounted=yes`.

Both QEMU profiles passed the event-wait and two-client checks. They
also passed the Core OS, named IPC and file-mapping suites. The event
tests reaped all child processes, released all handles and listeners,
and matched the original disk hash. The Windows updater passed install,
readback and rollback. The Pi passed both checks, restored all process
resources, and confirmed the mounted v5 generation 41.
[Pi capture](../proofs/pi5-event-wait-2026-11-26.txt)

## Phase 39 Named services

The monitor supervises up to three named EL0 services. The scheduler
runs them alongside foreground jobs and rotates running services when
the monitor is idle. A service's kernel process record uses pages
allocated at launch and released on shutdown.

| Monitor command | Result |
| --- | --- |
| `svc start -n NAME ELF [arguments]` | Registers and starts a service. Names accept up to 15 lowercase letters, digits, dots, underscores or hyphens. |
| `svc start -n NAME -d PROVIDER ELF [arguments]` | Starts a service with a running prerequisite. When the provider stops or exits, its dependent services stop in dependency order. |
| `svc list` | Lists configured names, dependencies, state, PIDs, launch and restart counts, and last stop details. |
| `svc status NAME` | Displays one service. |
| `svc stop NAME` | Stops a service and its dependents. |
| `svc restart NAME` | Loads the saved command and working directory with a fresh PID and retry budget. Its dependents can restart after the provider is running again. |

`svc start ELF [arguments]`, `svc` and `svc stop` address the default
service and use one of the three slots. Each service retries an
unsuccessful exit up to twice, with a 250 ms delay. A dependency tracks
its provider's process lifetime. An application can use `ports` to see
when a program has published its IPC listener. Stopped entries retain
their configuration and outcome until the slot is reused.

`ps -l` uses the 3712-byte version 4 process snapshot. It can show all
eight foreground slots and three services in one read. Version 1
through 3 process snapshot callers retain their original sizes. A
service reading a snapshot sees its own row.

### Try the Phase 39 update on Pi

With the Pi powered off and the boot pendrive mounted as `D:\`, run
these commands from the project folder in Windows PowerShell.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase39-services-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

Safely eject and boot the Pi. Enter `exit` at `user:/$`. Run each line
separately at the monitor `>` prompt.

```text
svc start -n alpha /boot/IPCECHO.ELF serve alpha
svc start -n beta -d alpha /boot/IPCECHO.ELF serve beta
svc start -n gamma /boot/IPCECHO.ELF serve gamma
svc list
appshell
```

Expect three active services. At `user:/$`, the pipeline should show
eleven process rows, including three marked `service`. Then exchange
one message with each service.

```text
ps -l | cat | cat | cat | cat | cat | cat
ipcecho call alpha one
ipcecho call beta two
ipcecho call gamma three
exit
```

At `>`, stop `alpha` and verify its dependent `beta` stopped.
Restart the independent `gamma`, then query it and check cleanup.

```text
svc stop alpha
svc status beta
svc restart gamma
svc status gamma
appexec /boot/IPCECHO.ELF call gamma restarted
svc stop gamma
appexec /boot/PORTS.ELF
fileinfo
storagetest
datacheck
```

Expect `beta` to show `enabled=no active=no`, `gamma` to show
`launches=2 restarts=1` with a new PID, and `IPC echo matched: restarted`.
After shutdown, expect `No listening ports.`, zero file handles, a storage
PASS and `matches_mounted=yes`.

The service suite passed on Cortex-A53/EL1 and Cortex-A76/EL2. It saw
all eleven process rows during a seven-stage pipeline, verified
independent and dependency-controlled restarts, and returned to the
initial page and handle counts. Both profiles also passed the Core OS,
event-wait, process capacity and process-tree regressions. The
Windows updater passed installation and rollback. Its Phase 38
preview lists `OUROS.IMG` and `PS.ELF`. The Pi showed all eleven process
rows, collected all ten foreground children, stopped `beta` with `alpha`,
and restarted `gamma` from PID 5 to PID 17. The restarted service replied
over IPC. The final checks showed zero listeners and open files, a
read-only storage PASS, and both v5 banks valid at generations 41 and 40.
[Pi capture](../proofs/pi5-services-2026-11-26.txt)

## Phase 40 Process permissions

An EL0 process carries a permission mask. Its children and pipeline
stages inherit the mask. `exec` retains it, and the supervisor uses the
configured mask on launch, retry, and restart. Ordinary launches use all
four permissions unless a restricted parent or explicit launch mask
sets fewer. Add the values of the permissions the process needs.

| Value | Permission |
| --- | --- |
| `1` | Change files or directories, including redirected file output. |
| `2` | Open and use UDP and TCP sockets. |
| `4` | Launch children or pipelines, subject to process limits. |
| `8` | Send and receive file descriptors through IPC. Writable descriptors also require `1` at both ends. |

With mask `0`, an app can read files, use the terminal and pass plain
IPC messages. A process can inspect its own mask or a direct child's
mask. It can permanently drop its own permissions with
`uos_policy_restrict()`. The kernel then closes any revoked writable
handles or sockets and discards queued attachments the recipient can
no longer accept. Other processes keep descriptors they already received.
A denied operation returns `UOS_EACCES`.

`policy [PID]` shows the mask at the EL0 shell. `runlim MASK PROGRAM
[args]` runs a child with a subset of the shell's permissions. At the
monitor, `svc start -n NAME [-d PROVIDER] -m MASK ELF [args]` configures a
restricted supervised service. `svc status` shows its configured launch
mask as `permissions=`.

### Try the Phase 40 update on Pi

Power off the Pi and connect its boot pendrive to Windows. Run these
lines from the project folder. They find the `OUROSBOOT` volume, preview
the update, and apply it after the preview succeeds.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase40-process-policy-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the pendrive, boot the Pi, and enter each line separately at
`user:/$`.

```text
policy
run /boot/LIMCHK.ELF
runlim 0 /boot/LIMCHK.ELF locked
policy
exit
```

Both `policy` calls should show `policy mask=15`. The two app runs should
print PASS lines and return `child exit=0`.

At the monitor `>` prompt, check inherited pipeline permissions, start a
restricted echo service, and check it again after a restart.

```text
appexec /boot/LIMCHK.ELF pipeline
svc start -n limited -m 0 /boot/LIMCHK.ELF service limited
svc status limited
appexec /boot/LIMCHK.ELF client limited
svc restart limited
svc status limited
appexec /boot/LIMCHK.ELF client limited
svc stop limited
appexec /boot/PORTS.ELF
fileinfo
storagetest
datacheck
```

Expect a pipeline PASS and two echo PASS lines. Both service status lines
should show `permissions=0`. The second should show a new PID and
`launches=2 restarts=1`. The final checks should show no listening ports,
zero open files, a read-only storage PASS, and `matches_mounted=yes`.

The permission suite passed on Cortex-A53/EL1 and Cortex-A76/EL2, with
zero open files or listeners after shutdown and matching disk hashes.
Both profiles passed the Core OS, descriptor, named IPC, event-wait,
service, UDP, and TCP regression suites. All 99 ZIP payload hashes matched
the tested image. The Windows updater passed preview, write verification,
and rollback. The Phase 39 preview adds `LIMCHK.ELF` and replaces
`OUROS.IMG` and `SHELL.ELF`. The Pi passed all permission checks. The
shell reaped all six children and kept mask 15. The restricted service
kept mask 0 across a restart from PID 12 to PID 14. Shutdown released
all listeners and handles, and both data banks passed validation.
[Pi capture](../proofs/pi5-process-permissions-2026-11-26.txt)
