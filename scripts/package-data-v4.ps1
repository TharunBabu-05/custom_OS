# Boot-file update only. Builds a ZIP in the project; never accesses a device.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$out=Get-ArtifactPath (Join-Path $project 'build/rpi5-header') $project
$apps=Get-ArtifactPath (Join-Path $project 'build/apps') $project
$manifest=Get-Content -Raw -LiteralPath (Join-Path $out 'OurOS-Pi5-header-experimental-sd.img.json') | ConvertFrom-Json
if ($manifest.console -cne 'header' -or $manifest.data_format -ne 4) { throw 'Build the header-console v4 image first' }
$work=Get-ArtifactPath (Join-Path $apps ('archive-check-'+[guid]::NewGuid().ToString('N'))) $apps
$files=Join-Path $work 'files';New-Item -ItemType Directory -Path $files | Out-Null
$records=@();$sum=[Text.StringBuilder]::new()
foreach ($entry in $manifest.files) {
    if ($entry.name -cne 'OUROS.IMG' -and $entry.name -cne 'LARGE.BIN' -and !([string]$entry.name).EndsWith('.ELF',[StringComparison]::Ordinal)) { continue }
    $source=if ($entry.name -ceq 'OUROS.IMG') { Join-Path $out 'ouros-rpi5.img' } elseif ($entry.name -ceq 'LARGE.BIN') {
        $fixture=Join-Path $work 'LARGE.BIN';[byte[]]$b=New-Object byte[] 16384
        for ($i=0;$i -lt $b.Length;$i++) { $b[$i]=[byte](($i*29+7)%256) };[IO.File]::WriteAllBytes($fixture,$b);$fixture
    } else { Join-Path $apps $entry.name }
    $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -cne $entry.sha256.ToLowerInvariant() -or (Get-Item -LiteralPath $source).Length -ne $entry.bytes) { throw "File differs from verified image: $($entry.name)" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $files $entry.name)
    $records+=@{name=$entry.name;bytes=$entry.bytes;sha256=$hash};$null=$sum.AppendLine($hash+'  '+$entry.name)
}
if ($records.Count -ne 55 -or @($records | Where-Object { $_.name -ceq 'OUROS.IMG' }).Count -ne 1) { throw 'Unexpected v4 boot-update file set' }
$record=[ordered]@{format='OurOS boot-file update v1';stage='data-v4';board='rpi5-header';
    source_disk_sha256=(Get-FileHash -LiteralPath (Join-Path $out 'OurOS-Pi5-header-experimental-sd.img') -Algorithm SHA256).Hash.ToLowerInvariant();
    accepted_previous_kernel_sha256=@('af25f87a94a7f188c16255fc5c2c01eb2e074f49af381da3f03c4487d308e8de');
    files=$records;changes_kernel=$true;changes_data_partition=$false;physical_acceptance='pending'}
[IO.File]::WriteAllText((Join-Path $files 'manifest.json'),($record | ConvertTo-Json -Depth 5),[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $files 'SHA256.txt'),$sum.ToString(),[Text.Encoding]::ASCII)
$readme=@'
OurOS Pi 5 header-console v4 driver/application boot update

This ZIP contains boot files, NOT a disk image. It updates OUROS.IMG and the
sample/test ELFs. It does NOT convert or format /data. Existing v1/v2/v3
volumes keep their old 4096/8192-byte file limits; only new v4 volumes allow
16384-byte files. The kernel and apps must be updated together for syscall43.
New v4 Pi hardware acceptance is pending; local tests are in docs/data-v4.md.

1. Power off the Pi, connect the existing pendrive to Windows and identify
   its FAT boot volume. Back up its boot files to a folder on the PC.
2. Get-FileHash X:\OUROS.IMG -Algorithm SHA256 (use the actual drive letter).
   Confirm the previous hash in manifest.json matches the preceding tested
   process-control/job installation. This package is for rpi5-header only.
3. Extract the ZIP on the PC. Copy OUROS.IMG, all *.ELF and LARGE.BIN into the
   FAT boot volume, replacing those files. Keep CONFIG.TXT, firmware/DTB and
   the separate /data partition in place. Do not flash this ZIP.
4. Safely eject, return the pendrive to the Pi and boot.

At the monitor, one command per line:
storageinit
datainit
mmuinit
irqinit
timerstart
apprun /boot/DF.ELF
appshell

df is also available as a shell external command. Existing files and shell
jobs keep working on old-format volumes. V4CHK intentionally refuses an old
volume before writing anything. BIGAPP is larger than 8 KiB, so it can run
from /boot but needs a v4 volume for local installation.

To use new v4 capacity: first save dataexport's complete serial output,
convert the log to a .bak and explicitly migrate it with TargetVersion4.
Build/flash a new v4 full image, then storageinit, datainit and datarestore
/boot/DATA.BAK on the empty new volume. See docs/data-v4.md. Reflashing a full
image replaces existing /data. This boot-file update avoids that reflash and
keeps the existing capacity. Neither packaging script writes physical media.
'@
[IO.File]::WriteAllText((Join-Path $files 'README.txt'),$readme,[Text.Encoding]::ASCII)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $work 'update.zip';[IO.Compression.ZipFile]::CreateFromDirectory($files,$temporary,[IO.Compression.CompressionLevel]::Optimal,$false)
$expanded=Join-Path $work 'expanded';[IO.Compression.ZipFile]::ExtractToDirectory($temporary,$expanded)
$expected=@($records | ForEach-Object { $_.name })+@('manifest.json','SHA256.txt','README.txt')
$actual=@(Get-ChildItem -LiteralPath $expanded -File | ForEach-Object { $_.Name })
if (Compare-Object ($actual | Sort-Object) ($expected | Sort-Object)) { throw 'Unexpected ZIP entries' }
foreach ($name in $expected) {
    if ((Get-FileHash -LiteralPath (Join-Path $files $name) -Algorithm SHA256).Hash -cne
        (Get-FileHash -LiteralPath (Join-Path $expanded $name) -Algorithm SHA256).Hash) { throw "ZIP extraction differs: $name" }
}
$destination=Get-ArtifactPath (Join-Path $apps 'OurOS-data-v4-boot-update.zip') $apps
Move-Item -LiteralPath $temporary -Destination $destination -Force
Write-Output 'PASS: v4 boot-file ZIP, exact entries/extracted hashes and verified full-image manifest agreement; no data partition included.'
Write-Output "UPDATE ZIP: $destination"
Write-Output ('ZIP SHA256: '+(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant())
