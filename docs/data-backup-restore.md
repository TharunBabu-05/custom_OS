# Persistent file backup and restore

Current images use [snapshot v4](data-v4.md). Old v1/v2/v3 archives require
explicit `migrate-data-backup.ps1 -TargetVersion 4` conversion before inclusion
in a default v4 image. Same-format restore and existing legacy mounts remain
supported. The historical examples below describe the flat v2 backup stage.

The [new Pi excerpt](../proofs/pi5-data-restore-2026-11-15.txt) now passes
DEMO.BAK restore, generation 2/two banks, file sizes and text readback. It does
not yet cover binary hashes, export/conversion, installed-shell execution or a
physical reboot. Current work is [file rename/stat](file-rename.md).

The [latest physical Pi capture](../proofs/pi5-usb-v2-capacity-2026-11-15.txt)
passes `usbcheck` and DATACAP (8192-byte files, sixteen slots, overflow and
cleanup). USB stays online after **152 recovered stalls**, with command/control
ring reuse and generation 51 in two valid banks. `stoageinfo` was a spelling
mistake; the corrected command succeeded. The retained sense fields are from
earlier rejected cache commands; the last transport result is OK and
`sense_valid=no`. This capture does not yet show installed-shell execution or
a physical power cycle.

This phase adds `dataexport`, `datarestore` and Windows conversion/build tools.
Only `/data` files are backed up. RAM root files disappear on reboot; `/boot`
remains read-only. The first upgrade from an older kernel does not gain export
until the new kernel runs. The posted DATACAP run leaves `/data` empty, so the
demonstration below starts with a new empty volume.

## Test the new image on the Pi

The image includes a **public demonstration** archive `/boot/DEMO.BAK` containing
`restore.txt` and an 8192-byte `pattern.bin`. It is not a backup of your files.
The archived backup-stage acceptance image also included `/boot/DATA.BAK` from
the automated demo roundtrip. Current default images include DEMO.BAK; DATA.BAK
is added when you build with `-DataBackup`.
After deploying the new full image, run at the kernel `>` prompt:

```text
storageinit
usbcheck
datainit
datarestore /boot/DEMO.BAK
datainfo
ls /data
cat /data/restore.txt
```

Expect `Data restore committed and read back`, generation 2, and
`OurOS verified backup restore.`. A second restore must refuse while `/data`
contains files; it does not overwrite them. DATACAP also refuses a nonempty
volume, so run it before restoring if you want to repeat that capacity test.

To check installation and stored binaries, continue at `>`:

```text
mmuinit
irqinit
timerstart
appinstall /boot/SHELL.ELF /data/shell.elf
appterm 60 /data/shell.elf
```

At `user:/$`:

```text
sha256 /data/pattern.bin
sha256 /boot/SHELL.ELF
sha256 /data/shell.elf
run /boot/HELLO.ELF
sync
exit
```

The two shell hashes must match. The pattern hash must be
`dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46`.
At `>`, check `fileinfo`, `heaptest`, `pagetest`, `storageinfo`, then `timerstop`.
Stop at a failure and preserve the diagnostic output.

Restart **without reflashing** and repeat storage/data/MMU/IRQ/timer setup.
Run `appterm 60 /data/shell.elf` and recheck the stored file hashes. This
separately establishes physical persistence; same-session reads are insufficient.

## Save your files before a later full-image update

Enable plain session logging in PuTTY (or another serial terminal), using a new
log under the project's ignored `backups` directory. At the kernel `>` prompt:

```text
dataexport
```

Wait for `-----END OUROS DATA BACKUP V1-----` and `Data export complete`.
At 115200 baud a full v2 export takes roughly half a minute plus overhead.
Save the raw log rather than copying wrapped terminal text. The records are
74 characters wide, so they also fit a normal 80-column terminal. Use a log
containing exactly one export; other console output outside its markers is OK.

From Windows PowerShell in the project root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/convert-data-backup.ps1 -Log backups/pi5-session.log -Output backups/pi5-data.bak
```

The converter checks contiguous offsets, row lengths, end marker, whole-archive
SHA-256, bank/header/payload checksums, exact format geometry, names, duplicate
entries, lengths and zero padding. It publishes a project-local `.bak` only
after validation and never overwrites an existing backup. A clipped/corrupt log
must be captured again, not edited to appear complete.

## Carry the backup into a new image

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataBackup backups/pi5-data.bak
```

The builder validates the archive and includes it as `/boot/DATA.BAK`, alongside
the demonstration archive. It creates a blank data partition; no restore runs
automatically. Deploy that image, then at `>`:

```text
storageinit
datainit
datarestore /boot/DATA.BAK
datainfo
```

Restore requires an **empty, healthy mounted volume of the same format**.
V1 backups require a `-DataVersion 1` image; the default image is v2. No format
conversion, resizing, merging or overwriting is performed. After restore, verify
file hashes and an actual reboot before discarding the old backup.

The `.bak`, its serial log and any image/ZIP containing it include your file
contents. Keep personal backups and resulting images private. SHA-256 detects
damage; it does not authenticate or encrypt a backup. These are local recovery
tools, not a package manager or an authenticated installer.

## Format and failure behavior

The binary backup is a normalized bank-zero header followed by its complete
payload: 33792 bytes for v1 or 132608 for v2. Existing bank magic, versions,
generation, payload SHA-256 and header SHA-256 remain in use. Unused fields and
file padding must be zero. The text wrapper adds contiguous 32-byte hex records
and an overall digest. Export uses the committed RAM snapshot without block I/O
or disk writes; a busy or failed volume refuses export.

Restore reads the immutable FAT file directly (archives exceed ordinary file
limits). It validates the full header, payload checksum and directory in bounded
staging memory before the first write. One existing snapshot commit publishes
all restored files together, using the destination's next generation rather
than adopting the source generation. An input/validation failure leaves the
volume unchanged. A commit I/O failure retains diagnostics and refuses further
writes until reboot; mounting selects a complete old or new bank. No automatic
formatting or raw-device access is introduced.

## Validation

Native tests cover both formats, binary/empty files, read-only export, nonempty
refusal, every interrupted input read and every interrupted restore commit.
Independent C/PowerShell interchange checks actual generated archives. Host
tests reject damaged/truncated/duplicate serial records, absent end markers,
checksummed malformed directories and overwrite attempts. QEMU A53/EL1 and
A76/EL2 pass the full demo restore/export/conversion cycle, rebuilding with that
archive as DATA.BAK, then restoring it in a fresh image. Stored-shell cold boot,
binary hashes, overwrite refusal and unchanged boot/unused sectors also pass.
Run `scripts/test-shell-qemu.ps1 -Backup` for DEMO.BAK or add
`-RestoreFile /boot/DATA.BAK` for the packaged archive. Eight image corruption/
profile rejection cases pass. Logs use `build/phase-backup-*.log`. New physical backup and
restore acceptance is pending.

Archived backup-stage image: 134217728 bytes; ZIP: 859884 bytes. Disk SHA-256:
`21f2e35c04aaa392fe380c7f8e2ba6cad640348c08ae4e318ff2f3329ccb2e0e`.
That kernel was 429812 bytes with reserved end `0x1c3000`, below the DTB at `0x200000`.
See the current file-rename guide for the latest delivery artifact.

Current full disk/ZIP artifacts remain in `image_files`, with earlier images
preserved. The next filesystem work is larger/nested persistent files and
version-aware migration; process/runtime and hardware work remain on the
[roadmap](remaining-os-roadmap.md).
