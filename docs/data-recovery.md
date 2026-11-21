# Inspect and recover an older /data snapshot

On the Pi 5 header build, update the boot files with the Windows updater.
The preview for a drive already running the automatic-startup build should
show `Replace OUROS.IMG`. Boot the Pi and type `exit` from the shell to reach
the monitor. [Boot update guide](boot-updates-tharun_verified.md)

```text
datacheck
```

`datacheck` reads both snapshot banks on the mounted drive. It reports valid
generations, empty banks, invalid headers or payloads, and read failures with
the failing sector. `matches_mounted=yes` means the current bank's generation
and bytes match the live filesystem. A newly flashed drive normally has one
valid bank and one empty bank. After a successful file change, both can be
valid. `datainfo` gives mount and write diagnostics if mounting failed.

## Save the older generation

When both banks are valid and the alternate bank has a lower generation,
it contains the filesystem before the last committed change. Start a fresh
serial capture and enter this command at the monitor.

```text
dataexport previous
```

Wait for `Data export complete` before closing the capture. A full v4
archive can take a minute at 115200 baud. Save the capture on the PC
and convert it to a verified backup for the existing empty-volume restore
workflow. Save a separate log if also exporting the current generation
with `dataexport`. The converter expects one complete archive per log.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/convert-data-backup.ps1 -Log backups/pi5-previous.log -Output backups/pi5-previous.bak
```

Both commands use reads only. The current mounted files stay available.
The previous generation is replaced by later commits, so export it before
making more changes if you need its contents. Use an empty volume of the
matching format to restore an archive. [Backup and restore guide](data-backup-restore.md)

## Verification

The host regression checks v1 through v4, read errors, corrupted banks,
export interrupted during its second read and round-trip restoration.
The QEMU regression writes two generations on a scratch image, checks
and exports the earlier one, then damages the newest payload and verifies
fallback on reboot. It compares disk hashes around the read-only sessions.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-persist-v4.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -DataRecovery
```

The Pi verified generations 11 and 10 on the SanDisk v4 volume. It exported
generation 10 to Windows, where the converter and the native restore test
verified the archive. [Pi check and export](../proofs/pi5-v4-persistence-2026-11-21.txt)
