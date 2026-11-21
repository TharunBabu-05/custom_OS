# Update a Pi 5 header boot volume from Windows

Build the kernel and matching user programs into one boot-file ZIP. This
build uses the existing AArch64 tools in `.dist` and creates
`build/rpi5-header/OurOS-Pi5-header-boot-update.zip`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-rpi5-update.ps1
```

Power off the Pi and connect the drive to Windows. Use the `OUROSBOOT` drive
letter shown in Explorer. With the project open in PowerShell, preview the
update and then apply it. Substitute the current drive letter for `D:\`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Apply
```

The updater checks the package hashes, `CONFIG.TXT` and the pinned Pi DTB.
It copies only changed or missing files from the package, verifies their
hashes on the boot volume and saves the previous versions in `backups`.
It adds `CMDLINE.TXT` with `ouros.startup=shell` if absent and preserves any
existing command line. The separate `/data` partition stays in place.
Safely eject the drive before reconnecting it to the Pi.

## Recover the previous boot files

Use the backup folder printed during installation. The restore preview
lists the previous files to copy back and any newly added files to remove.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -RestoreFrom 'backups/pi5-boot-<printed-suffix>'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -RestoreFrom 'backups/pi5-boot-<printed-suffix>' -Apply
```

Keep the PC backup until the Pi boots successfully. An interruption can leave
a mixture of old and new files on FAT. Restore the recorded backup on the
same drive before retrying. The updater requires the stock header configuration
and matching DTB. Review firmware or configuration changes separately.

## Which files to update

| Change | Existing compatible drive |
| --- | --- |
| Kernel only | Replace `OUROS.IMG`. |
| Apps or kernel ABI | Copy the matching changed `.ELF` files with the kernel when required. |
| Startup mode | Edit `CMDLINE.TXT` to select shell or monitor. |
| New drive or changed partition layout | Use a complete image or the documented migration process. Export any existing `/data` first. |

The Windows regression exercises preview, install, repeat install and rollback
on scratch boot files. Pi hardware evidence covers the preceding two-file
update, automatic shell startup, shell exit and `V4CHK`. The remaining
physical v4 checks are an installed `BIGAPP.ELF` and a matching 16 KiB file
hash across a power cycle. [Pi capture](../proofs/pi5-startup-v4chk-2026-11-21.txt)
