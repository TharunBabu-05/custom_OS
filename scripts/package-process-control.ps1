# Boot-file update for the preceding Pi 5 header memory/allocator image.
# Produces workspace artifacts only; never opens a physical drive.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$out=Get-ArtifactPath (Join-Path $project 'build/rpi5-header') $project
$apps=Get-ArtifactPath (Join-Path $project 'build/apps') $project
$imageManifest=Get-Content -Raw -LiteralPath (Join-Path $out 'OurOS-Pi5-header-experimental-sd.img.json') | ConvertFrom-Json
if ($imageManifest.console -cne 'header') { throw 'Header-console image required' }
$work=Get-ArtifactPath (Join-Path $out ('archive-check-'+[guid]::NewGuid().ToString('N'))) $out
$files=Join-Path $work 'files';New-Item -ItemType Directory -Path $files | Out-Null
$payload=@()
foreach ($item in @(@{Name='OUROS.IMG';Source=(Join-Path $out 'ouros-rpi5.img')},@{Name='PS.ELF';Source=(Join-Path $apps 'PS.ELF')})) {
    $source=Get-ArtifactPath $item.Source $project
    $digest=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $entry=@($imageManifest.files | Where-Object { $_.name -ceq $item.Name })
    if ($entry.Count -ne 1 -or $entry[0].sha256.ToLowerInvariant() -cne $digest -or
        $entry[0].bytes -ne (Get-Item -LiteralPath $source).Length) { throw "Source differs from the full-image manifest: $($item.Name)" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $files $item.Name)
    $payload+=@{name=$item.Name;bytes=$entry[0].bytes;sha256=$digest}
}
$previous='72852aa77b033b9f6330b0f93a8f9dbcbff8d9c864568db792d51d3946813065'
$manifest=[ordered]@{format='OurOS boot-file update v1';stage='process-control';board='Pi 5';console='header';
    expected_previous_kernel_sha256=$previous;files=$payload;new_syscalls=@(40,41,42);
    data_formats_unchanged=@(1,2,3);physical_process_control_acceptance='pending'}
[IO.File]::WriteAllText((Join-Path $files 'manifest.json'),($manifest | ConvertTo-Json -Depth 5),[Text.Encoding]::ASCII)
$readme=@'
OurOS process control - boot-file update, NOT a flashable disk image

For the preceding Pi 5 header-console MEMCHK/HEAPCHK-stage installation.
It preserves that installation's firmware, CONFIG.TXT, PI5.DTB and /data.
PS.ELF needs the new kernel. Copy both payload files, not PS.ELF alone.
No formatting, reflashing or on-device filesystem conversion is needed.

1. Power off the Pi and connect its existing pendrive to Windows.
2. Extract this ZIP on the PC. Open the existing FAT boot volume containing
   OUROS.IMG, CONFIG.TXT and PI5.DTB.
3. Confirm the CURRENT OUROS.IMG SHA256 matches expected_previous_kernel_sha256
   in manifest.json. In PowerShell: Get-FileHash X:\OUROS.IMG -Algorithm SHA256
   Replace X: with the actual boot-volume letter. If it differs, this update
   has not established compatibility with that installation; use the complete
   image workflow after preserving user data, or review the mismatch first.
4. Copy the old OUROS.IMG to a backup folder on the PC. Then copy the NEW
   OUROS.IMG and PS.ELF from this ZIP to the boot volume root, replacing only
   OUROS.IMG (and PS.ELF if already present). Leave all other files unchanged.
5. Wait for copying to finish, safely eject the drive and return it to the Pi.

On a fresh Pi boot, enter one command per line at the kernel monitor:
storageinit
mmuinit
irqinit
timerstart
appterm 120 /boot/PS.ELF check

Expect two PASS lines, code=0, heap_live_pages=0 and cleanup=restored.
Then try: appterm 120 /boot/PS.ELF demo
At process> enter status, stop, start, status, stop, exit on separate lines.
The parent remains interactive while its one child runs. stop cancels and
collects the child (130), while wait preserves a prior exit/timeout result.
Children are limited to 30 seconds; the whole session remains bounded.
In appshell, ps shows a coherent snapshot of the active batch.
datainit is optional for these tests. No test files are written to /data.

This phase is locally tested; new Pi process-control acceptance is pending.
General shell background jobs, process trees and indefinite services remain
later work. The old allocator-only ZIP does not install this kernel API.
'@
[IO.File]::WriteAllText((Join-Path $files 'README.txt'),$readme,[Text.Encoding]::ASCII)
$hashes=@($payload | ForEach-Object { $_.sha256+'  '+$_.name })
[IO.File]::WriteAllText((Join-Path $files 'SHA256.txt'),(($hashes -join "`r`n")+"`r`n"),[Text.Encoding]::ASCII)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $work 'update.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($files,$temporary,[IO.Compression.CompressionLevel]::Optimal,$false)
$zip=[IO.Compression.ZipFile]::OpenRead($temporary)
try {
    $actual=@($zip.Entries | ForEach-Object { $_.FullName } | Sort-Object)
    $expected=@('OUROS.IMG','PS.ELF','README.txt','SHA256.txt','manifest.json' | Sort-Object)
    if (($actual -join '|') -cne ($expected -join '|')) { throw 'Unexpected update ZIP contents' }
} finally { $zip.Dispose() }
$expanded=Join-Path $work 'expanded';[IO.Compression.ZipFile]::ExtractToDirectory($temporary,$expanded)
foreach ($name in $expected) {
    if ((Get-FileHash -LiteralPath (Join-Path $files $name) -Algorithm SHA256).Hash -cne
        (Get-FileHash -LiteralPath (Join-Path $expanded $name) -Algorithm SHA256).Hash) { throw "Update ZIP extraction differs: $name" }
}
$destination=Get-ArtifactPath (Join-Path $out 'OurOS-process-control-boot-update.zip') $out
Move-Item -LiteralPath $temporary -Destination $destination -Force
Write-Output 'PASS: boot-update exact contents, extracted hashes and payload agreement with the full-image manifest.'
Write-Output "UPDATE ZIP: $destination"
Write-Output ('ZIP SHA256: '+(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant())
Write-Output 'Use the included previous-kernel hash check; copy both OUROS.IMG and PS.ELF with the Pi powered off. /data is not replaced.'
