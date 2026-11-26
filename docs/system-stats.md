# Phase 48 system statistics

Run `sysstat` in the EL0 shell for a read-only snapshot of system activity.
The report includes uptime in milliseconds, delivered and skipped timer
periods, unexpected interrupts, and the number of secondary workers ready,
parked, faulted, or in transition. It shows physical memory usage, kernel
heap usage and allocation failures, active foreground apps and services,
runnable, sleeping and blocked task counts, open file handles, hardware
resource claims, and mount status.

`sysstat watch 3` samples three times at roughly 250 ms intervals and
reports timer changes, free pages, and task counts. `sysstat check` verifies
the ABI and basic accounting from EL0. Output can go through a pipeline
or into a file. Process counts include the caller. Extra processes and
file handles used by pipelines and redirection can appear in the report.

Applications can use `uos_system_stats` from the user runtime. Syscall 95
accepts an output pointer and the exact size of the 176-byte v1 result.
The kernel validates the full destination before copying. The result
contains aggregate counts. Worker states are read as the snapshot is
collected, so a worker transitioning during a read can appear in its old
or new state. Timer counters are relative to the most recent timer start.

## Pi 5 acceptance

Power off the Pi and connect its boot pendrive. From the project folder
in Windows PowerShell, preview and apply the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase48-system-stats-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the pendrive, boot the Pi, and run these at `user:/$`.

```text
sysstat
sysstat check
sysstat watch 3 | cat
exit
```

Expect `PASS: system statistics ABI, destination checks and accounting.`
and three samples with positive `timer_delta` values. At `>`, run:

```text
meminfo
resinfo
smpstart
smppark 0x200
appexec /boot/SYSSTAT.ELF
smpstop
appexec /boot/SYSSTAT.ELF
meminfo
resinfo
fileinfo
storagetest
datacheck
```

The two reports should show two ready and one parked worker, then zero
of each. The monitor should return to the starting page and claim
counts, with zero open files and successful storage checks.

The Cortex-A53 and Cortex-A76 QEMU tests exercise both mount states,
worker park and fault states, a blocked service, restricted app access,
output redirection, and timer samples in a pipeline. The Pi 5 passed
the ABI check, timer sampling, parked-worker reporting and shutdown.
[Phase 48 Pi capture](../proofs/pi5-system-stats-2026-11-26.txt)
