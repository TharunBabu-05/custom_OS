# Application-only update for the Pi-tested process-control kernel.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$apps=Get-ArtifactPath (Join-Path $project 'build/apps') $project
$out=Get-ArtifactPath (Join-Path $project 'build/rpi5-header') $project
$required='af25f87a94a7f188c16255fc5c2c01eb2e074f49af381da3f03c4487d308e8de'
if ((Get-FileHash -LiteralPath (Join-Path $out 'ouros-rpi5.img') -Algorithm SHA256).Hash.ToLowerInvariant() -cne $required) { throw 'Kernel differs from the Pi-tested process-control stage' }
$source=Get-ArtifactPath (Join-Path $apps 'SHELL.ELF') $apps
$digest=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=Get-Content -Raw -LiteralPath (Join-Path $out 'OurOS-Pi5-header-experimental-sd.img.json') | ConvertFrom-Json
$entry=@($manifest.files | Where-Object { $_.name -ceq 'SHELL.ELF' })
if ($manifest.console -cne 'header' -or $entry.Count -ne 1 -or $entry[0].sha256.ToLowerInvariant() -cne $digest -or
    $entry[0].bytes -ne (Get-Item -LiteralPath $source).Length -or $entry[0].bytes -gt 8192) { throw 'Shell differs from the full-image manifest or installation bound' }
$work=Get-ArtifactPath (Join-Path $apps ('archive-check-'+[guid]::NewGuid().ToString('N'))) $apps
$files=Join-Path $work 'files';New-Item -ItemType Directory -Path $files | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $files 'SHELL.ELF')
$record=[ordered]@{format='OurOS application-only update v1';stage='shell-jobs';required_kernel_sha256=$required;
    files=@(@{name='SHELL.ELF';bytes=$entry[0].bytes;sha256=$digest});kernel_unchanged=$true;physical_shell_jobs_acceptance='pending'}
[IO.File]::WriteAllText((Join-Path $files 'manifest.json'),($record | ConvertTo-Json -Depth 5),[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $files 'SHA256.txt'),($digest+"  SHELL.ELF`r`n"),[Text.Encoding]::ASCII)
$readme=@'
OurOS shell background jobs - application-only update

For the preceding Pi 5 header-console process-control installation that passed
PS.ELF check/demo. The kernel and calls 0..42 are unchanged in this phase.
Required OUROS.IMG SHA256 is recorded in manifest.json.

1. Power off the Pi and connect its existing pendrive to Windows.
2. Extract this ZIP on the PC. Check the current boot volume's OUROS.IMG hash:
   Get-FileHash X:\OUROS.IMG -Algorithm SHA256 (use its actual drive letter).
   It must match required_kernel_sha256 in manifest.json.
3. Back up the old SHELL.ELF to a folder on the PC. Copy the new SHELL.ELF
   beside OUROS.IMG on the existing FAT boot volume, replacing SHELL.ELF.
   Leave the kernel, configuration, firmware and /data unchanged.
4. Safely eject, return the pendrive to the Pi and boot normally.

At the kernel monitor, enter one command per line:
storageinit
mmuinit
irqinit
timerstart
appshell

At user:/$ try:
bg 30 /boot/SPIN.ELF
jobs
echo shell-is-responsive
stop
jobs
exit

bg uses a 1..30 second child deadline. jobs reports pending/done without
collecting; fg waits/collects and hands terminal input to the child; stop
cancels/collects. A completed result is preserved. One outstanding job is
allowed: collect it before another external command, exec or pipeline.
Filesystem/env/echo builtins remain usable while the child runs.
Parent exit, Ctrl+C or session timeout also reclaims its outstanding child.

For background file input/output after datainit:
bgio 30 - /data/job.txt echo hello-background
fg
cat /data/job.txt
Do not overwrite an existing output path. '-' keeps the default stream.

This ZIP is copied as files; it is not a flashable disk image. No formatting
or reflashing is needed. This changes only the boot SHELL.ELF; an older shell
installed separately under /data remains a separate executable. Use appupdate
explicitly if you want to update that copy as well.
New shell-job Pi acceptance is pending. Local acceptance is recorded in the
phase guide. General process trees, multiple jobs and indefinite services are
not introduced by this update.
'@
[IO.File]::WriteAllText((Join-Path $files 'README.txt'),$readme,[Text.Encoding]::ASCII)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $work 'update.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($files,$temporary,[IO.Compression.CompressionLevel]::Optimal,$false)
$zip=[IO.Compression.ZipFile]::OpenRead($temporary)
try {
    $actual=@($zip.Entries | ForEach-Object { $_.FullName } | Sort-Object)
    $expected=@('SHELL.ELF','README.txt','SHA256.txt','manifest.json' | Sort-Object)
    if (($actual -join '|') -cne ($expected -join '|')) { throw 'Unexpected shell-update ZIP contents' }
} finally { $zip.Dispose() }
$expanded=Join-Path $work 'expanded';[IO.Compression.ZipFile]::ExtractToDirectory($temporary,$expanded)
foreach ($name in $expected) {
    if ((Get-FileHash -LiteralPath (Join-Path $files $name) -Algorithm SHA256).Hash -cne
        (Get-FileHash -LiteralPath (Join-Path $expanded $name) -Algorithm SHA256).Hash) { throw "ZIP extraction differs: $name" }
}
$destination=Get-ArtifactPath (Join-Path $apps 'OurOS-shell-jobs-update.zip') $apps
Move-Item -LiteralPath $temporary -Destination $destination -Force
Write-Output 'PASS: application-only shell ZIP, exact entries/extracted hashes, full-image manifest agreement and unchanged Pi-tested kernel.'
Write-Output "UPDATE ZIP: $destination"
Write-Output ('ZIP SHA256: '+(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant())
Write-Output 'Copy only SHELL.ELF to the existing boot volume; kernel and /data are retained.'
