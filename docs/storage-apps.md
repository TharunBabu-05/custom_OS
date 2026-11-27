# Phase 32 Storage and apps

## Serial console on Windows

Close PuTTY to release COM3. From an interactive PowerShell or Windows
Terminal tab, run one of these commands.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/serial-console.ps1 -Port COM3 -Log build/pi-phase32.log
& 'C:\Program Files\PuTTY\plink.exe' -serial COM3 -sercfg '115200,8,n,1,N'
```

The PowerShell console sends characters at 10 ms intervals and logs Pi
output. Use Ctrl+Shift+V to paste one command in Windows Terminal. Wait
for the next Pi prompt before pasting again. Press Ctrl+] to close the
PowerShell serial console.

## Signed packages

A signed `.OPR` repository holds up to eight releases and fits in
64 KiB. A release includes up to four files of up to 32 KiB each,
plus up to three exact-version dependencies. Include `main.elf`
when the release has an executable. A release can also contain
data files alone. Up to four packages can be active on a mounted
v5 volume, within its available nodes and extents.

The repository signature uses ECDSA P-256 over the complete
catalog and payloads. `pkgtrust` pins the SHA-256 of the release
signer's public key for the current boot. The installer verifies
the signature, each file digest, the ELF, and the requested dependency
graph before staging. It checks the staged files by reading them
back, then publishes the active selection in one v5 commit. An
interrupted install can leave files in an inactive slot. A retry
reclaims that slot before staging.

| Monitor command | Effect |
| --- | --- |
| `pkgtrust <SHA-256>` | Pin the public-key digest from the repository publisher |
| `pkgtrust clear` | Clear the pin |
| `pkginstall /boot/REPO.OPR pkgdemo 1` | Install version 1 with its dependencies |
| `pkglist` | Print the active versions |
| `pkgstart pkgdemo` | Verify the installed files and run the executable |
| `pkgpath pkgbase` | Print the active directory of a dependency |
| `pkgremove pkgdemo` | Unregister and reclaim an app's releases |

Packages use `uos_package_path` to find installed assets. Only the
monitor installer can modify the package store during normal use.
Removing a package required by another active release is refused.
A package upgrade can change several dependency selections in one
commit when they remain compatible with the rest of the active set.
Named ELF applications retain their existing `appadd`, `appupgrade`,
`approllback`, and `appuninstall` commands.

The same signed archive can be copied to `/boot` or fetched to
`/data`. The existing pinned TLS test server accepts `-Package`
with an `.OPR` file of up to 32 KiB. It prints a `PKGGET.ELF`
command that saves `/data/DLREPO.OPR`. After the download, pin the
release key and use `pkginstall /data/DLREPO.OPR pkgdemo 1`.
[TLS setup](package-download.md)

### Build a repository

Create a P-256 key once, then build from a JSON manifest. In
PowerShell, use the following commands with paths for your project.

```powershell
& 'C:/msys64/usr/bin/openssl.exe' genpkey -algorithm EC -pkeyopt ec_paramgen_curve:prime256v1 -out build/release-signing.pem
python scripts/build-repository.py --manifest build/repository.json --key build/release-signing.pem --output build/REPO.OPR
```

Source paths are relative to the manifest. Share the repository
and the printed public-key pin through separate trusted channels.
Keep the signing key in the build environment.

```json
{
  "packages": [
    {"name": "demo", "version": "1", "files": {"main.elf": "apps/HELLO.ELF"},
     "requires": [{"name": "assets", "version": "1"}]},
    {"name": "assets", "version": "1", "files": {"version.txt": "version.txt"}}
  ]
}
```

For the linked demo, `scripts/build-storage-apps-demo.ps1` generates a
fresh test repository and a `PIN.TXT` file. Use the pin paired with
the repository in your image or update.

## Pi acceptance on the current v5 drive

The named-app lifecycle, signed repository, and clean power-cycle
checks can run on the current v5 drive after its Phase 32 boot-file
update. Use a fresh app name and a new retention file. Run the
[named-app commands](#migrate-a-v4-backup-and-test-a-named-app)
starting at `appadd`, and use an existing data file with a known
digest for the before-and-after checks. The
[repository commands](#verify-a-signed-repository) work on the same
drive.

## Pi acceptance using the spare image

Write `build/rpi5-header/OurOS-Pi5-header-phase65-storage-acceptance.img`
to a spare USB drive. The Phase 65 image uses the current kernel and the
Phase 32 storage fixtures. The image is 128 MiB.
Flashing erases the selected target. The image contains a v4 demo
archive, a blank v5 data partition, the signed demo repository,
and the spare-media interruption marker.

Read the matching public-key pin from Windows when you reach the
repository check.

```powershell
Get-Content build/rpi5-header/OurOS-Pi5-header-phase32-signing-pin.txt
```

For the checks on the current v5 drive, apply the following update
with the drive mounted as `D:\`.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase32-storage-apps-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory D:\ -Package $pkg -Apply
```

### Migrate a v4 backup and test a named app

Boot the spare drive. At `user:/$`, type `exit`. Run these at
the `>` monitor prompt, waiting for each command to finish.

```text
datarestore /boot/DEMO.BAK
appadd phase32check 1 /boot/ECHO.ELF
appstart phase32check managed-one
appupgrade phase32check 2 /boot/HELLO.ELF
appstart phase32check
approllback phase32check
applist
appshell
```

Expect `managed-one`, the HELLO program's greeting, and
`phase32check active=1 previous=2`. At the shell, check the
imported binary and write a separate retention marker.

```text
sha256 /data/pattern.bin
write /data/power32.txt retained-v5
exit
datacheck
poweroff
```

The expected SHA-256 is
`dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46`.
When shutdown completes, physically remove power, then restore
power and boot the same spare drive. At `user:/$`, run:

```text
cat /data/power32.txt
sha256 /data/pattern.bin
exit
datacheck
appstart phase32check managed-rollback
appuninstall phase32check
applist
```

Expect `retained-v5`, the same digest, `matches_mounted=yes`,
`managed-rollback`, and `Uninstalled phase32check`.

### Verify a signed repository

At the monitor, paste the `pkgtrust` command from the paired Windows
`PIN.TXT` file. Then run:

```text
pkginstall /boot/REPO.OPR pkgdemo 1
pkglist
pkgstart pkgdemo
pkgremove pkgbase
pkginstall /boot/REPO.OPR pkgdemo 2
pkgstart pkgdemo
pkgremove pkgdemo
pkgremove pkgbase
fileinfo
```

The first start prints the two revision-one assets. Removal of
`pkgbase` is refused while `pkgdemo` depends on it. The second
start prints the revision-two assets. The last file count is zero.

### Test an interrupted write

For the standalone migration check on freshly flashed media, run `df /data`
and `cat /boot/CUTTEST.TAG` in the startup shell. Confirm an empty v5 `/data`
and the spare-media marker. Run `exit`, `datarestore /boot/DEMO.BAK`, and
`datacheck` at the monitor. Then run `appshell`, `sha256 /data/pattern.bin`,
and `cat /data/restore.txt`. Check the digest above and the text
`OurOS verified backup restore.`

At the monitor, run `appshell`. At `user:/$`, prepare two committed
versions of the dedicated test file. If the migration check left you in the
shell, continue there.

```text
write /data/cut32.txt older-cut
write /data/cut32.txt before-cut
cat /data/cut32.txt
exit
datacheck
datacut
```

`datacut` is enabled by the spare image marker. It invalidates the
previous bank and verifies the staged replacement data, then stops
before publishing the new index. When it prints `CUT READY`,
physically power off the Pi and boot from the spare drive again.

```text
cat /data/cut32.txt
sha256 /data/pattern.bin
cat /data/restore.txt
exit
datacheck
appshell
write /data/cut32.txt after-boot
cat /data/cut32.txt
exit
datacheck
fileinfo
storagetest
```

Expect `before-cut`, one valid mounted bank and one empty bank, and
then `after-boot` from the new post-remount write. On a fresh Phase 65
image the import commits generation 2. The two test writes reach
generation 4. The power-cut remount must keep generation 4 and the
post-remount write must commit generation 5.

## Verification

Both Cortex-A53 and Cortex-A76 QEMU profiles passed the migration
from a v4 backup, named-app selection and removal, repository
signature and dependency checks, and a remount after the staged
interruption. A separate package test held the registry open,
verified the old selection after publication was refused, and
passed the upgrade retry after remount. Native persistence tests also passed injected read,
write, and flush failures. The Windows TLS fixture transferred the
signed repository byte for byte. The boot updater passed preview,
apply, readback, and rollback against a Phase 31 directory copy.

| Artifact | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase65-storage-acceptance.img` | `0e34198dc89cc1ece9cea3b230c69dc195590fbb041d8682245176a0e1a3a5f8` |
| `OurOS-Pi5-header-phase32-spare-acceptance.img` | `e1794c6225a23f6dcd4cd86dffbda0951e16e3b2748dd2020c116c3326c30ce3` |
| `OurOS-Pi5-header-phase32-storage-apps-boot-update.zip` | `3d333d86afb893d5e372e8c581faed84497e0b4278f31bc8dd9cf89d15f8d07f` |

The physical Pi passed named-app install, upgrade, rollback, and
uninstall, plus signed-package installation, dependency protection, and
upgrade. After a cold power cycle the SanDisk remounted generation 26.
Both installed apps ran and the recovered Phase 31 files still read
`current` and `older`. After removing the test apps, the Pi reported
zero open files and a matching generation 41.
[Phase 32 capture](../proofs/pi5-storage-apps-2026-11-24.txt)

Phase 65 used the verified spare image to import the v4 archive into an
empty v5 volume. The Pi committed generation 2 and both restored files
matched the fixture. After `CUT READY` and physical power removal, the Pi
remounted generation 4, returned `before-cut`, and reproduced the imported
file digest and text. The follow-up write committed generation 5, both banks
validated, and the USB read test passed with zero open files.
[Phase 65 capture](../proofs/pi5-storage-migration-power-cut-2026-11-27.txt)
