# Phase 6 local application lifecycle

The kernel monitor manages named applications on a mounted v3, v4 or v5
`/data` volume. Use `exit` from the shell to return to the monitor. Starting
an application needs the usual MMU and timer setup, which automatic startup
already performs.

| Command | Effect |
| --- | --- |
| `appadd <name> <version> <ELF>` | Validate, stage, and install a new managed app |
| `applist` | Show the active and previous versions |
| `appstart <name> [arguments]` | Verify the active ELF and run it as a bounded foreground EL0 app |
| `appupgrade <name> <version> <ELF>` | Stage a different version and retain the former active version |
| `approllback <name>` | Verify the previous ELF and swap the active and previous versions |
| `appuninstall <name>` | Remove the app registration and that app's stored versions |

Names use up to 20 lowercase letters, digits, underscores or hyphens and
start with a letter or digit. Version labels use up to 15 characters and can
also contain uppercase letters, dots and plus signs. Versions are labels
and either installed label can be selected by rollback. The existing
`appinstall`, `appupdate` and `apprun` path-based commands remain available.

## Storage and recovery

`/data/.apps` is reserved for managed apps. Each app uses one metadata record
and up to three ELF slots. The record stores the selected and previous slots,
their version labels and the SHA-256 digest of each executable. The mounted
filesystem's node count, free space and maximum file size govern deployment.
Keep user files outside this directory.

An upgrade checks the active ELF and validates the candidate using the same
parser as the EL0 loader. It writes and reads back the candidate in an
unselected slot, then publishes the new record in one filesystem commit.
A failed staging operation leaves the selected record readable. An I/O
failure during the final commit can leave either complete record on disk.
Reboot after an ambiguous storage failure and use `applist` to see which
version was selected. Rollback verifies the stored digest and ELF before
switching versions. An ELF may still fail at runtime. Use `approllback` to
select the previous version if that happens.

Uninstall first removes the record, then removes the app's version files.
If interrupted, rerun `appuninstall <name>` to reclaim the remaining slots.
The removal step requires no spare data extent. The shared `/data/.apps`
directory stays available for future installs. Remove it with
`rmdir /data/.apps` if it is empty and you need an entirely empty volume
for a Phase 5 backup restore.

## Pi verification

Apply the Phase 6 boot-file package to the existing `OUROSBOOT` volume.
Set `$boot` to its Windows drive letter. The first command previews the
changes. The second applies them and saves a rollback copy. The preview
against the Phase 5 image selected only `OUROS.IMG`.
[Windows boot update](boot-updates-tharun_verified.md)

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase6-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Boot the Pi and type these commands one at a time from the shell and monitor.

```text
exit
appadd phase6check 1.0 /boot/ECHO.ELF
appstart phase6check "version one"
appupgrade phase6check 2.0 /boot/HELLO.ELF
appstart phase6check
appupgrade phase6check 3.0 /boot/CONFIG.TXT
applist
approllback phase6check
reboot
```

After startup, return to the monitor. Confirm that version 1.0 is selected,
then remove the test app.

```text
exit
applist
appstart phase6check "version one after reboot"
appuninstall phase6check
applist
```

## Verification results

Native ELF parsing passed 25,000 deterministic mutations. The QEMU
`-AppLifecycle` suite passed on v4 with Cortex-A53/EL1 and v5 with
Cortex-A76/EL2. It checked invalid and duplicate versions, a blocked
record write after staging, cold-boot selection, rollback, uninstall,
and retention of an unrelated user file. The Windows updater passed
preview, apply, and rollback on scratch files. The Pi later installed
`netdemo` 1.0 from the downloaded ELF, ran it with exit code 0, and
listed the active version. Pi upgrade, rollback and uninstall checks
remain open. [Pi package capture](../proofs/pi5-package-download-2026-11-22.txt)

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase6-boot-update.zip` | `12e3b01353291dfe388ab1e26c9d035f64ae7dd83c4a417d7cb7ce0417c7327f` |
| `OUROS.IMG` | `d820d47016a0fc376a3c9f518f9667bd10d435f10a379a58c2f170c0e5bafb4a` |
