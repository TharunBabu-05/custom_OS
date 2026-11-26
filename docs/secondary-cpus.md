# Secondary CPU workers

The monitor can use PSCI to start up to three secondary cores from the
enabled device-tree CPU entries. Each worker enters EL1 with a private
4 KiB stack, installs the kernel's verified identity mappings, leaves
its interrupts masked, and waits for work. The boot CPU continues
to own the EL0 scheduler, device drivers, and physical page allocator.

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

## Run on Pi 5

Power off the Pi and connect its boot pendrive to Windows. Run this
from the project folder in PowerShell. The Phase 42 update replaces
`OUROS.IMG` when the drive already has Phase 41.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase42-worker-recovery-boot-update.zip'
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
smpstart
smptest
smpfault 0x100
smpinfo
smptest
smpstart 0x100
smptest
smppark 0x200
smptest
smpstart 0x200
smpsubmit 16
smpinfo
smpwait
smpsubmit 1024
smpcancel
smptest
smpstop
cpustate
meminfo
fileinfo
storagetest
datacheck
```

Expect checksum job counts of 3, 2, 3, 2, 3, and 3, including the
result from `smpwait`. The faulted worker should show
`state=fault-parked`, `psci=on`, and ESR `0xf2000041`, then report
`resumed at EL1`. The cancellation should print
`Secondary batch cancelled.`. After `smpstop`, `cpustate` should
report the three secondaries off and `meminfo` should return to its
first `allocated_pages` count. `fileinfo` should show zero open
files, `storagetest` should pass, and `datacheck` should show
`matches_mounted=yes`.

## Phase 42 verification

Four-core Cortex-A53/EL1 and Cortex-A76/EL2 QEMU runs passed
targeted startup, park and resume, breakpoint recovery, batch result
verification, repeat cancellation, and poweroff with a batch pending.
The boot CPU continued to run the shell and timer. Both profiles
verified that cancellations leave the completed-job counts unchanged
and that final shutdown restores the initial page count. The v5 test
images matched their original hashes after the worker checks.

The Core OS, process policy, event wait, named IPC, service and storage
suite regressions passed on both profiles. Both Pi kernel builds fit
with 4 KiB before the firmware device tree. All 99 ZIP payload hashes
matched the verified image. The updater verified installation and
rollback and selected only `OUROS.IMG` from Phase 41. Phase 42 Pi
verification is pending.
