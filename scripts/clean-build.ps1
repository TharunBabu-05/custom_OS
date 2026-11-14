# Only known generated files are eligible. Back up completed disk images first.
[CmdletBinding()]
param([string]$BuildDirectory,[string]$ArchiveDirectory,
      [switch]$IncludeTestImages,[switch]$IncludePreviewCaches)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
if (!$BuildDirectory) { $BuildDirectory=Join-Path $project 'build' }
if (!$ArchiveDirectory) { $ArchiveDirectory=Join-Path $project 'image_files' }
$BuildDirectory=Get-ArtifactPath $BuildDirectory $project
$permittedBuild=Join-Path $project 'build'
if ($BuildDirectory -ne $permittedBuild -and !$BuildDirectory.StartsWith($permittedBuild+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'BuildDirectory must be the project build tree or a subdirectory' }
$ArchiveDirectory=Get-ArtifactPath $ArchiveDirectory $project
if (!(Test-Path -LiteralPath $BuildDirectory)) { return }
& "$PSScriptRoot/archive-images.ps1" -BuildDirectory $BuildDirectory -ArchiveDirectory $ArchiveDirectory
# A complete safe plan is checked before the first removal. No symlinks/junctions.
$targets=@()
foreach ($profile in @('rpi5','rpi5-header','qemu_virt','handoff-test','apps')) {
    $directory=Join-Path $BuildDirectory $profile
    if (!(Test-Path -LiteralPath $directory)) { continue }
    $current=if ($profile -eq 'rpi5') { 'OurOS-Pi5-experimental-sd.img' } else { 'OurOS-Pi5-header-experimental-sd.img' }
    foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
        if ($item.PSIsContainer -and $item.Name -cmatch '^(disk-stage|image-check|image-negative|boot-files|archive-check|corruption-test)-[0-9a-f]{32}$') { $targets+=$item.FullName }
        elseif (!$item.PSIsContainer -and $item.Name -cmatch '^OurOS-.*\.img(?:\.json|\.sha256)?$' -and $item.Name -notin @($current,"$current.json","$current.sha256")) {
            $original=$item.FullName -replace '\.(json|sha256)$',''
            if (Test-Path -LiteralPath $original -PathType Leaf) { $targets+=$item.FullName }
        }
        elseif (!$item.PSIsContainer -and $item.Extension -eq '.o') { $targets+=$item.FullName }
    }
}
if ($IncludeTestImages) {
    foreach ($name in @('wifi-tests','usb-tests')) {
        $dir=Join-Path $BuildDirectory $name
        if (Test-Path -LiteralPath $dir) { $targets+=@(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -ieq '.img' } | ForEach-Object { $_.FullName }) }
    }
}
if ($IncludePreviewCaches) {
    foreach ($name in @('readme-chrome','readme-chrome-preview')) { $dir=Join-Path $BuildDirectory $name;if (Test-Path -LiteralPath $dir) { $targets+=$dir } }
}
$freed=0L;$files=0
foreach ($target in $targets) {
    $resolved=Get-ArtifactPath $target $BuildDirectory
    $entry=Get-Item -LiteralPath $resolved -Force
    $contents=if ($entry.PSIsContainer) { @(Get-ArtifactFiles $resolved) } else { @($entry) }
    foreach ($file in $contents) {
        $null=Get-ArtifactPath $file.FullName $BuildDirectory
        if ($file.Name -cmatch '^OurOS-Pi5(?:-header)?-.+\.img$' -and (Test-CompleteDiskImage $file.FullName)) {
            $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $matches=@(Get-ChildItem -LiteralPath $ArchiveDirectory -File -Filter '*.img' | Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hash })
            if (!$matches.Count) { throw "Disk image has no verified backup; refusing cleanup: $resolved" }
        }
        $freed+=$file.Length;++$files
    }
}
foreach ($target in $targets) {
    $resolved=Get-ArtifactPath $target $BuildDirectory
    Remove-Item -LiteralPath $resolved -Force -Recurse
}
Write-Output "Cleaned $files generated files ($freed bytes); current images, kernels/ELFs/maps, latest logs and archive retained."
