# One-time migration from the superseded hash-folder archive to flat disk images.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$archive=Get-ArtifactPath (Join-Path $project 'image_files') $project
$indexPath=Get-ArtifactPath (Join-Path $archive 'index.json') $archive
if (!(Test-Path -LiteralPath $indexPath)) { throw 'Legacy index not found; no migration performed' }
$index=Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
$current=Join-Path $project 'build/rpi5-header/OurOS-Pi5-header-experimental-sd.img'
$currentHash=(Get-FileHash -LiteralPath $current -Algorithm SHA256).Hash.ToLowerInvariant()
$plan=@();$unnamed=0
foreach ($record in $index.images | Where-Object { $_.kind -eq 'disk' }) {
    if ($record.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid archive identity' }
    $source=Get-ArtifactPath (Join-Path $archive "$($record.sha256)/image.img") $archive
    if (!(Test-CompleteDiskImage $source) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw "Disk image identity check failed: $source" }
    $named=@($record.origins | Where-Object { $_ -cmatch '(^|/)OurOS-Pi5(?:-header)?-.+\.img$' } | ForEach-Object { [IO.Path]::GetFileName($_) } | Sort-Object -Unique)
    if ($named.Count) { $name=$named[0] }
    else { ++$unnamed;$name='OurOS-Pi5-header-earlier-build-'+$unnamed.ToString('00')+'.img' }
    if ($name -eq 'OurOS-Pi5-header-experimental-sd.img' -and $record.sha256 -ne $currentHash) {
        if ($record.phases -contains 'wifi-auth-foundation-hardware-verified') { $name='OurOS-Pi5-header-wifi-auth-foundation-verified.img' }
        else { ++$unnamed;$name='OurOS-Pi5-header-earlier-build-'+$unnamed.ToString('00')+'.img' }
    }
    $destination=Get-ArtifactPath (Join-Path $archive $name) $archive
    if (@($plan | Where-Object { $_.destination -eq $destination }).Count) { throw "Duplicate restored name: $name" }
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw "Existing file conflicts with restored name: $name" }
    $plan+=[pscustomobject]@{source=$source;destination=$destination;name=$name;sha256=$record.sha256;origins=$record.origins}
}
if (!$plan.Count) { throw 'No completed disk images found; nothing removed' }
# Resolve and inspect every legacy directory before any move/delete.
$legacy=@(Get-ChildItem -LiteralPath $archive -Directory | Where-Object { $_.Name -cmatch '^[0-9a-f]{64}$' })
foreach ($folder in $legacy) { $null=Get-ArtifactPath $folder.FullName $archive;$null=@(Get-ArtifactFiles $folder.FullName) }
foreach ($entry in $plan) {
    # Moving within image_files preserves the original image bytes without another copy.
    if (!(Test-Path -LiteralPath $entry.destination)) { Move-Item -LiteralPath $entry.source -Destination $entry.destination }
    if ((Get-FileHash -LiteralPath $entry.destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw 'Restored image hash mismatch; legacy directories retained' }
}
$report=Join-Path $project 'build/image-backup-migration.json'
$plan | Select-Object name,sha256,origins | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $report -Encoding UTF8
# All complete images have been restored and hash-checked. Remove the old layout,
# including raw kernels/test artifacts and catalog files the user did not request.
foreach ($folder in $legacy) {
    $resolved=Get-ArtifactPath $folder.FullName $archive
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
foreach ($name in @('index.json','README.md','latest-rpi5-header.json','latest-rpi5-debug.json')) {
    $path=Get-ArtifactPath (Join-Path $archive $name) $archive
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
Write-Output "Restored $($plan.Count) complete disk images directly into image_files; removed $($legacy.Count) hash folders and extra catalog files."
Get-ChildItem -LiteralPath $archive -File | Select-Object Name,Length
