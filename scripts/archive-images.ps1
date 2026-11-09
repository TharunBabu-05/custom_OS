[CmdletBinding()]
param([string]$Image,
      [string]$BuildDirectory,[string]$ArchiveDirectory)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
if (!$BuildDirectory) { $BuildDirectory=Join-Path $project 'build' }
if (!$ArchiveDirectory) { $ArchiveDirectory=Join-Path $project 'image_files' }
$BuildDirectory=Get-ArtifactPath $BuildDirectory $project
$permittedBuild=Join-Path $project 'build'
if ($BuildDirectory -ne $permittedBuild -and !$BuildDirectory.StartsWith($permittedBuild+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'BuildDirectory must be the project build tree or a subdirectory' }
$ArchiveDirectory=Get-ArtifactPath $ArchiveDirectory $project
if ($ArchiveDirectory -eq $BuildDirectory -or $ArchiveDirectory.StartsWith($BuildDirectory.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Archive must be outside the build tree being archived' }
New-Item -ItemType Directory -Force -Path $ArchiveDirectory | Out-Null
$images=if ($Image) { @(Get-Item -LiteralPath (Get-ArtifactPath $Image $BuildDirectory)) } else { @(Get-ArtifactFiles $BuildDirectory | Where-Object { $_.Name -cmatch '^OurOS-Pi5(?:-header)?-.+\.img$' -and (Test-CompleteDiskImage $_.FullName) }) }
foreach ($entry in $images) {
    $null=Save-DiskImageBackup $entry.FullName $BuildDirectory $ArchiveDirectory
}
Write-Output "Complete disk image backups verified: $($images.Count). Saved directly in $ArchiveDirectory"
