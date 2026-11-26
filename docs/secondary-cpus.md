# Secondary CPU workers

The monitor can use PSCI to start up to three secondary cores from the
enabled device-tree CPU entries. Each worker enters EL1 with a private
4 KiB stack, installs the kernel's verified identity mappings, leaves
its interrupts masked, and waits for work. The boot CPU runs the EL0
scheduler, device drivers, and physical page allocator.

## Phase 41 Pi results

The Pi started three workers and verified their checksums. It captured
the first worker's test exception, observed the core off, and released
its stack. The other two cores continued to work. During restart,
affinity `0x100` stayed at `psci=pending`. The kernel retained one stack
page because the startup could still complete. USB storage checks passed
and both v5 data banks were valid. [Pi capture](../proofs/pi5-secondary-cores-2026-11-26.txt)

## Phase 42 Restart and work controls

Phase 42 uses an in-kernel park and resume cycle for same-boot worker
restarts. Parked cores wait for events and keep their stacks. Their
firmware status stays `psci=on`. The Pi starts each core once with PSCI
and requires a new boot after `smpstop` powers it off. QEMU also tests a
second PSCI launch after power-down.

| Monitor command | Behavior |
| --- | --- |
| `smpstart [0xAFFINITY]` | Start or resume selected workers. The default selects all known secondaries. A manual monitor boot needs `mmuinit` first. |
| `smppark [0xAFFINITY]` | Pause a worker or all workers and keep their stacks available for resume. |
| `smpfault [0xAFFINITY]` | Run the test breakpoint on one online worker and park it with its fault record. By default it selects the first online worker. |
| `smpinfo` | Show worker and firmware states, launches, resumes, completed jobs, stacks, faults, and pending batch replies. |
| `smpsubmit [ROUNDS]` | Dispatch a batch to the online workers and return to the monitor. Choose 1 to 1024 checksum rounds per worker. The default is 1. |
| `smpwait` | Collect and verify the outstanding batch, including results and stack canaries. |
| `smpcancel` | Discard the outstanding results and wait for cancellation acknowledgements. Workers check for cancellation between rounds. |
| `smptest` | Submit and verify one round in a single command. |
| `smpstop` | Discard pending work, request firmware power-down, and release stacks after firmware reports the cores off. |

The boot core publishes mailbox requests with release stores. Workers
use acquire loads before reading inputs and release stores to publish
their replies. The boot CPU issues one outstanding batch at a time.
Each worker uses its own record and stack. The exception handler returns
past the known test instruction so the worker can enter the parking
loop. An unexpected worker exception still takes the power-down path.
PSCI calls supply the physical entry address and its context pointer.
The entry code validates the context and hardware affinity before
using the stack. EL1 and EL2 entry both lead to the same EL1 worker.
Firmware reports an accepted startup as pending until it transitions
the core to on.
[Arm PSCI specification](https://documentation-service.arm.com/static/6703a8b8d7e4b739d817e10d)

PMM changes happen on the boot core. If startup or shutdown cannot be
verified, the boot core retains the stack page so it cannot be reused
while a secondary might still access it. `smpinfo` shows retained
stacks. Both caches remain off under the existing Normal-NC profile.
`reboot`, `poweroff`, and `halt` request worker shutdown.

## Phase 42 verification

The Pi passed all six checksum batches, resumed both parked workers
on their original stacks, and acknowledged cancellation. After
`smpstop`, PSCI reported all three secondaries off. Allocations returned
to two pages, open handles returned to zero, USB reads passed, and v5
generations 41 and 40 were valid. The commands ran at the monitor
after exiting the EL0 shell. [Pi capture](../proofs/pi5-worker-recovery-2026-11-26.txt)

Four-core Cortex-A53/EL1 and Cortex-A76/EL2 QEMU runs also passed
targeted startup, park and resume, breakpoint recovery, batch result
verification, repeat cancellation, and poweroff with a batch pending.

## Phase 43 Interprocessor interrupts

After starting the workers, use `smpirq on` to claim GICv2 SGI 15.
Each worker derives its target from its own banked GIC registers and
saves the local CPU interface, priority and enable state. The boot CPU
checks that the resulting targets are distinct. A worker acknowledges
the IRQ with the full GIC token, including the sender CPU, and publishes
the reply sequence. [Arm GICv2 specification](https://documentation-service.arm.com/static/5f8ff196f86e16515cdbf969?token=)

| Monitor command | Behavior |
| --- | --- |
| `smpirq on` | Opt in to worker interrupt delivery after `irqinit` and `smpstart`. Repeat after starting additional cores. |
| `smpipi [0xAFFINITY]` | Send an interrupt to the selected worker or to all resident workers. Wait for each selected CPU's acknowledgement. |
| `smptlb` | Invalidate the boot CPU's EL1 translations and request the same maintenance on every resident worker, including parked cores. Verify all acknowledgements. |
| `smpinfo` | Show worker SGI state, GIC target, completed IPI and TLB counts, and the last acknowledged GIC token. |
| `smpirq off` | Mask worker IRQ delivery, clear the owned SGI's pending state, restore each worker interface, and release the claim after verification. |

The basic TLB check exercises IRQ delivery and local invalidation against
the current kernel mappings. Checksum dispatch uses the event mailbox.
If an acknowledgement times out, use `smpirq off` to recover the routes.
Worker shutdown also releases SGI ownership after the cores are off.

### Phase 43 verification

The host GIC tests verified SGI setup rollback, pending-state cleanup,
full interrupt tokens, and retry after a failed restore. Four-core
Cortex-A53/EL1 and Cortex-A76/EL2 QEMU runs verified targeted delivery,
broadcast, all four TLB acknowledgements, and shutdown with SGIs
enabled. Both profiles restored worker allocations and resource
claims. The worker lifecycle, periodic timer, peripheral IRQ, exec,
and file mapping regressions passed.

Both Pi builds fit with 4 KiB before the firmware device tree.
All 99 update payload hashes matched the v5 image. The updater
passed installation and rollback on a scratch directory.

The Pi received one targeted reply and three broadcast replies. The
timer reported zero unexpected interrupts. Shutdown returned to two
allocated pages and eight resource claims. USB reads and both data
banks passed. The Phase 44 procedure includes the remaining physical
`smptlb` check. [Pi capture](../proofs/pi5-interprocessor-irqs-2026-11-26.txt)

## Phase 44 Shared kernel page remapping

`smptlb map` clones the kernel's translation root, shares the existing
identity mappings, and adds a single test virtual address backed by
two temporary data pages. Every resident worker with a verified SGI
route switches to the test root, including parked and fault-parked
workers. The monitor requires collection or cancellation of any
outstanding checksum batch before the test starts.

Every participating core reads page A. The monitor invalidates the
leaf descriptor and waits for each CPU to invalidate the address
locally and observe a level-3 translation fault. It installs page B
only after those replies. Each core reads the new contents and
checks with `AT` that an EL1 write and an EL0 read would fault. The
test revokes the alias again, then restores every CPU's original
TTBR0 and releases all five pages. [Arm AArch64 memory management guide](https://documentation-service.arm.com/static/6ac508d3f59e042617c76666)

If a worker misses an acknowledgement, the boot CPU restores its
root and retains the test pages. `smpirq off` can reclaim the pages
after each worker confirms restoration. `smpstop` can reclaim them
once the secondaries are verified off. General multicore mappings
for applications remain roadmap work.

### Phase 44 verification

Four-core Cortex-A53 and Cortex-A76 QEMU runs passed both remaps,
including parked and fault-parked workers. The tests verified that
incomplete SGI setup and a pending batch leave allocation counts
unchanged. File copying, `exec`, file mappings, MMU protection,
and timer, USB and v5 data checks passed.

A scratch build suppressed a worker's acknowledgement after it entered
the test root. The boot CPU returned to its original root and retained
five pages. Separate runs reclaimed them with `smpirq off` and `smpstop`.
Both Pi builds fit with 32 KiB before the firmware device tree.
All 99 package payload hashes matched the v5 image.

The Pi passed `smptlb` and all five `smptlb map` stages with three
online workers. Both attempts to park a worker were rejected. The
Phase 45 test includes the parked and fault-parked cases and final
resource and storage checks. [Pi capture](../proofs/pi5-smp-page-remap-2026-11-26.txt)

### Run Phase 44 on Pi 5

Power off the Pi and connect its boot pendrive to Windows. Run this
from the project folder in PowerShell. On a Phase 43 drive the updater
selects `OUROS.IMG`. Use the existing pendrive.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase44-smp-page-remap-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the pendrive and boot the Pi. At `user:/$`, type `exit`.
At the monitor `>` prompt, enter one line at a time.

```text
meminfo
resinfo
smpstart
smpirq on
smpipi 0x100
smpipi
smppark 0x200
smptlb
smpinfo
meminfo
smptlb map
smpinfo
meminfo
smpstart 0x200
smpfault 0x300
smptlb map
smpstart 0x300
smptest
mmutest
timertest
irqinfo
smpirq off
resinfo
smpstop
cpustate
meminfo
resinfo
fileinfo
storagetest
datacheck
```

Expect 1 and 3 acknowledgements from the first two IPI commands.
`smptlb` should print `PASS: SMP TLB invalidations=4` while
`0x200` is parked. Each `smptlb map` should print
`SMP map CPUs A/unmapped/B/revoked/restored=4/4/4/4/4` and the
remap PASS. The two `meminfo` readings around the first remap
should report the same allocation count. After `smpirq off`, resource
claims should match the baseline. After `smpstop`, all three workers
should be off, allocated pages should match the first `meminfo`,
`fileinfo` should show zero open files, and storage checks should pass.

If a remap reports retained pages, run `smpirq off`, `smpstop`,
`meminfo`, `resinfo`, `storagetest`, and `datacheck` and save the
UART output.

## Phase 45 Shared worker queue

The workers draw jobs from one shared queue. A bounded lock protects
job claims, and each worker computes its own results outside the lock.
The lock uses Lamport's bakery algorithm with one aligned atomic word
per CPU and a wait deadline. It uses ordered loads and stores in the
Normal-NC memory profile. [Lamport's paper](https://lamport.azurewebsites.net/pubs/bakery.pdf)

| Monitor command | Behavior |
| --- | --- |
| `smplock` | Hold the lock on the boot CPU, wait for every online worker to time out, then verify that the lock can be acquired again. |
| `smpqueue [JOBS]` | Submit 1 to 256 checksum jobs to the online workers and return to the monitor. Defaults to 64. |
| `smpinfo` | Show completed queue jobs and worker replies while a batch is outstanding. |
| `smpwait` | Verify the checksum and owner of every queue entry, then report each worker's contributions. |
| `smpcancel` | Discard the queue and wait for workers to acknowledge cancellation. |

If a worker fails to acknowledge completion or cancellation, the monitor
reserves the queue state until `smpstop` verifies worker shutdown. Affinity
arguments now accept both `0x` and `0X`. The boot CPU owns EL0 scheduling.
The `workers=` count reports workers that acknowledged the batch. Each
`queue_jobs=` line reports that worker's actual contribution.

### Phase 45 verification

Four-core Cortex-A53 and Cortex-A76 QEMU runs passed lock timeouts,
queue bounds, exact result verification, parked-worker exclusion,
cancellation, and shutdown. The worker lifecycle, remap, Core OS,
timer, and storage checks also passed. The lock object uses Armv8.0
`LDAR` and `STLR` for the tickets.

A scratch build paused a worker inside a claim to test cancellation
while a queue was unfinished. A second fault stopped a worker while
it held the lock. Queue submission was refused until `smpstop`
verified shutdown, and a new queue passed after restart in QEMU.
Both Pi builds fit with 28 KiB before the firmware device tree.
All 99 package payload hashes matched the verified image.

The Pi verified all 256 jobs, with contributions of 86, 85, and 85.
It passed cancellation and queue reuse, both parked remaps, timer
delivery, USB reads, and both data banks. A UART receive error
preceded the storage commands. Phase 46 includes the final shutdown
and resource checks. [Pi capture](../proofs/pi5-smp-work-queue-2026-11-26.txt)

### Run Phase 45 on Pi 5

Power off the Pi and connect its boot pendrive to Windows. From the
project folder, run this in PowerShell. The preview selects `OUROS.IMG`
and `SHELL.ELF` on a Phase 44 boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase45-smp-work-queue-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the pendrive and boot the Pi. Type `exit` at `user:/$`.
Enter the following commands at the monitor `>` prompt, one line at a time.

```text
meminfo
resinfo
smpstart
smplock
smpqueue 256
smpinfo
smpwait
smpirq on
smppark 0X200
smptlb
smptlb map
smpinfo
smplock
smpqueue 23
smpwait
smpfault 0x300
smptlb map
smpstart 0X200
smpstart 0x300
smpqueue 64
smpcancel
smpqueue 32
smpwait
timertest
smpirq off
smpstop
cpustate
meminfo
resinfo
fileinfo
storagetest
datacheck
```

The lock check should report three timeouts before parking and two
after parking. The queues should verify `256 workers=3`,
`23 workers=2`, and `32 workers=3`. The parked core should appear in
`smpinfo` and both remaps should report `4/4/4/4/4`. After cancellation,
the next queue should pass. After `smpstop`, all three workers should
be off, page allocations and resource claims should match the baseline,
and the storage checks should pass.

## Phase 46 Worker physical page leases

The boot CPU services allocation and release requests from one mailbox
per worker. It owns the bitmap and the lease records. A worker publishes
its request with a release store and reads the acknowledgement with an
acquire load before using the granted span. [Arm memory ordering guide](https://documentation-service.arm.com/static/62a304f231ea212bb662321d)

- `page_alloc_aligned` allocates runs with a power-of-two alignment in pages.
- `page_free_run` checks the full span before returning any pages. Process
  page tables, service contexts and remap scratch tables use this release path.
- A worker can hold four runs and sixteen pages. Each release must match
  the owning worker, base address, and exact page count.
- The broker zeros pages before granting them and scrubs them before
  returning them to the physical allocator.
- `smpmem [ROUNDS]` checks aligned allocation, ownership and quota rejections,
  and data preservation in simultaneous runs. It defaults to four rounds
  and accepts 1 to 16.
- `smpmeminfo` prints per-worker live and peak pages, allocation and release
  totals, rejections, and pages reclaimed after failures.

If a worker stops acknowledging requests, `smpmem` retains its leases
and refuses to reuse the mailbox. `smpstop` releases those pages and
the worker's stack after firmware confirms the CPU is off. The boot
allocator can service its ordinary callers while those pages are reserved.

### Phase 46 verification

Native tests verified aligned allocation across reserved holes and
atomic rejection of invalid releases, including runs spanning bitmap
words. The four-core Cortex-A53 and Cortex-A76 tests covered all worker
counts from one to three and the 16-round limit. They checked ownership
rejections, quota counters, data integrity, zero live leases, and the
original page and resource counts after shutdown.
The existing worker queue, Core OS, file mapping, and executable
replacement suites passed on both profiles.

A scratch build faulted one worker after it received ten pages. The
broker retained those pages while the boot allocator passed `pagetest`.
After PSCI confirmed shutdown, `smpstop` reclaimed all ten pages. A new
worker batch passed on restart in QEMU. Both Pi builds fit with 24 KiB
before the firmware device tree. All 99 ZIP payload hashes matched the
verified v5 image, and the updater passed rollback checks.

### Run Phase 46 on Pi 5

Power off the Pi and connect its boot pendrive to Windows. From the
project folder, run the boot updater. The preview selects `OUROS.IMG`
and `SHELL.ELF` on a Phase 45 boot volume.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase46-physical-page-leases-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the pendrive and boot the Pi. Type `exit` at `user:/$`.
At the `>` prompt, enter one command at a time and wait for the next prompt.

```text
meminfo
resinfo
smpstart
smpirq on
smpmem 4
smpmeminfo
smppark 0X200
smpmem 2
smpstart 0X200
smpqueue 32
smpwait
timertest
smpirq off
smpstop
cpustate
meminfo
resinfo
smpmeminfo
fileinfo
storagetest
datacheck
```

Expect `PASS: SMP memory workers=3 rounds=4` followed by
`PASS: SMP memory workers=2 rounds=2`. Each finished worker should show
`memory_pages=0` and `retained=no`. After `smpstop` all three secondary CPUs
should be off, page allocations and resource claims should match the initial
readings, and both storage checks should pass. Phase 46 Pi verification
is pending.
