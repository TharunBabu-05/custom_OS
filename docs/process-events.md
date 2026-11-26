# Phase 49 process history

The kernel keeps the latest 32 lifecycle events in memory across shell
sessions. It records committed launches, successful image replacements
with the same PID, stops with reason and exit status, and reaping after
resource cleanup. Each entry includes uptime in milliseconds, PID,
parent PID, and the root, child, pipeline, or service role. Failed
launch preparation and failed image loads leave the history unchanged.

Use `procevt` in the EL0 shell for retained events from the current
foreground session. A service querying the interface sees its own
events. At the kernel monitor, `proclog` shows the complete retained
history. Records carry numeric IDs and status fields. Command
arguments, paths, register values, and kernel addresses are excluded.

`procevt since N` resumes after sequence N. Copy N from a previous
`cursor=` line. `procevt watch 4` starts at the current tail and samples
for new events four times, about 250 ms apart. Both forms support pipes
and redirection. Listing can show the reader's own launch. Watch
starts with the cursor after its launch. The history resets on reboot.
`expired=` counts
overwritten positions in the global sequence, including any positions
outside the reader's scope.

Syscall 96 takes the destination pointer, exact result size, and the last
sequence read. It returns up to eight records in a versioned 480-byte
page. The cursor advances through both visible and filtered records.
Use `uos_process_events` from the user runtime.

Native tests cover wraparound, pagination, and reader visibility. Both
QEMU profiles verified stable PIDs through exec, a timeout arriving
during watch, successful cleanup, and a full 32-record monitor history.

## Pi 5 acceptance

Power off the Pi and connect its boot pendrive. From the project
directory in Windows PowerShell, preview and apply the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase49-process-events-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject and boot the pendrive. Run the following at `user:/$`,
waiting for each prompt.

```text
procevt check
run /boot/EXEC.ELF /boot/HELLO.ELF
run /boot/FAULT.ELF
bg 15 /boot/SPIN.ELF
stopall
bg 1 /boot/SPIN.ELF
waitall
echo event-pipe | cat
procevt since 0
procevt watch 2 | cat
exit
```

The shell history should include an `exec child` event, normal exit,
the deliberate EL0 fault with code 125, cancellation with code 130,
timeout with code 124, and pipeline records with `cleanup=ok`.
Run these at the kernel monitor.

```text
proclog
meminfo
resinfo
fileinfo
storagetest
datacheck
```

Expect the final shell exit and reap in `proclog` and zero open handles.
The monitor keeps 32 rows after the history wraps. The overwritten
count can vary with the number of commands you run.
