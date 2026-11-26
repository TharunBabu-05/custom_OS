# Phase 50 process timers

An app or supervised service can open up to four timers. The kernel has
16 slots shared by all processes. Each handle belongs to the process
that opened it, and a closed handle stays invalid when its slot is reused.
Process stops and successful image replacements release every timer owned
by that process. A failed image load keeps its timers.

The runtime exposes `uos_timer_open`, `uos_timer_arm`, `uos_timer_arm_at`,
`uos_timer_info`, `uos_timer_read`, `uos_timer_disarm`, and `uos_timer_close`
through syscall 97. Relative delays are in milliseconds. Absolute
deadlines use the same clock as `uos_time_ms`. Future deadlines can be up
to one hour ahead. A past absolute deadline becomes ready immediately.
An interval of zero gives a one-shot timer. A positive interval of up
to one hour repeats at the scheduled cadence. Missed periods accumulate
in the expiration count. Rearming and disarming clear unread counts.

`UOS_EVENT_TIMER` can share an event set with a child, signal, or IPC
source. READ means at least one expiration is pending. HANGUP means the
handle is closed or belongs to a different process. A readiness check
leaves the pending count available for `uos_timer_read`, which consumes
the count or returns `UOS_EAGAIN` when empty. Task wakeups follow
the existing scheduler tick, so a blocked caller can wake after its deadline.

## Pi 5 acceptance

Power off the Pi and connect its boot pendrive. From the project directory
in Windows PowerShell, preview and apply the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase50-user-timers-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject and boot the pendrive. Run these at `user:/$`. Wait for each
prompt before sending the next line.

```text
tmrchk
tmrchk once 75 | cat
tmrchk fault
bg 1 /boot/TMRCHK.ELF hold
waitall
exit
```

Expect the timer check to pass and the one-shot demo to report at least
75 ms. The deliberately faulting app exits with code 125. The background
app reaches its process deadline and exits with code 124.

Run these at the kernel monitor.

```text
timerobjects
svc start -n timers /boot/TMRCHK.ELF hold
timerobjects
svc restart timers
timerobjects
svc stop timers
timerobjects
fileinfo
storagetest
datacheck
```

The timer counts should be 0, 1, 1, and 0. The final storage checks
should pass and the open file count should be zero.

Native tests cover timer arithmetic, quotas, ownership, and handle reuse.
Both QEMU profiles passed the timer and existing event-wait suites.
The 75 ms one-shot measured 79 ms on Cortex-A53 and 82 ms on Cortex-A76.
Both profiles returned to zero timers after service shutdown.
The Pi measured 75 ms for the one-shot. The service ran with one timer
before and after restart and released it at shutdown.
[Phase 50 Pi capture](../proofs/pi5-user-timers-2026-11-26.txt)
