# Boot to the serial shell

The Pi 5 header-console disk image requests an interactive shell at power-up.
The kernel mounts read-only FAT32 at `/boot` and opens the existing snapshot
at `/data`. It then enables the EL1 MMU, starts the 100 Hz timer and launches
`/boot/SHELL.ELF`. The shell loads a valid `/data/session.env` if present.
The first interactive prompt is normally `user:/$`. Type `df` for the mount
table or `exit` for the kernel monitor.

## Original two-file update for the SanDisk installation

For future releases, use the [Windows boot updater](boot-updates.md).

1. Power off the Pi and connect the drive to Windows. Open the FAT volume
   containing `CONFIG.TXT`, `OUROS.IMG` and `SHELL.ELF`. Save the current
   `OUROS.IMG` and any existing `CMDLINE.TXT` to the PC for rollback.
2. Extract the [locally built two-file update](../build/rpi5-header/OurOS-startup-boot-update.zip).
   Copy `OUROS.IMG` and `CMDLINE.TXT` to the root of that FAT volume.
   The original v4 image shipped without `CMDLINE.TXT`. On that image,
   Windows will ask to replace `OUROS.IMG` and add `CMDLINE.TXT` as a new file.
   If the command line already has custom settings, keep them on one line and
   set exactly one `ouros.startup=shell` entry. This update uses the ELFs from
   the preceding v4 installation and only touches the FAT boot volume.
3. Safely eject, reconnect the drive to the Pi and capture the UART from power-on.
   Expect `Starting serial shell from /boot/SHELL.ELF`, the two mount reports
   and the shell prompt. Enter one command per line.

```text
df /data
exit
pwd
```

The v4 drive should report `max_file_bytes=16384`. After `exit`, expect
`App resources restored; monitor ready.` and a `>` prompt. Monitor `pwd`
should print `/`. Type `startup` to enter the shell again using the existing
mounts, MMU and timer. Each session has the existing 300-second terminal
budget. Exit, timeout or a refused prerequisite returns control to the monitor.

For manual boot, change the setting to `ouros.startup=monitor` or remove the
startup token. Unknown or repeated startup settings also select the monitor.
Pi firmware reads `cmdline.txt` by default. [Raspberry Pi firmware documentation](https://www.raspberrypi.com/documentation/computers/config_txt.html#cmdline)

The kernel reads the firmware command line from `/chosen/bootargs`. A missing
or invalid data volume stops startup before the shell. The monitor diagnostics
`storageinfo` and `datainfo` show which stage needs attention.

To roll back the kernel, restore the saved boot files and the prior command
line on the PC. For a **new** drive, the complete header disk image includes
an empty v4 partition. Flashing a complete image replaces the disk contents.

## Reproduce on Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -Startup
```

`-KeepBuildFiles` leaves the recovery build and test disks in place. The
image and update ZIP are verified before promotion.

The startup regression uses scratch copies on Cortex-A53 and Cortex-A76.
It covers manual setup followed by startup, automatic profile loading after
a fresh boot, reuse of a running timer and return to the monitor. It also
checks the recovery prompt with an invalid data superblock or disk partition table and compares disk
hashes around read-only sessions. Logs are under `build/handoff-test/startup-*.log`.

The Pi capture verifies automatic startup from the two-file update, `df /data`,
shell exit to the monitor and a successful `V4CHK.ELF` run. The v4 guide gives
the commands for an installed larger ELF and a retained file hash across
power-off. [Pi capture](../proofs/pi5-startup-v4chk-2026-11-21.txt) · [v4 guide](data-v4.md)
