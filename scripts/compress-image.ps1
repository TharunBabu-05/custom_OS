# ZIP contains exactly one complete disk image, never just the small kernel.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Image,
    [string]$ArchiveDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'image_files')
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$build=Join-Path $project 'build'
$source=Get-ArtifactPath $Image $project
if ($source -cnotmatch '(?i)\\(?:build|image_files)\\' -or
    [IO.Path]::GetFileName($source) -cnotmatch '^OurOS-Pi5(?:-header)?-.+\.img$' -or
    !(Test-CompleteDiskImage $source)) { throw 'Compression requires a complete named 128 MiB OurOS disk image in build or image_files' }
$archive=Get-ArtifactPath (Join-Path $ArchiveDirectory '.path-check') $project
$archive=Split-Path $archive -Parent
$defaultArchive=Join-Path $project 'image_files'
if ($archive -ne $defaultArchive -and !$archive.StartsWith($build+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'ZIP output must be in image_files or a build test directory' }
New-Item -ItemType Directory -Force -Path $archive | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Read-ZipDiskHash([string]$Path) {
    $zip=[IO.Compression.ZipFile]::OpenRead($Path)
    try {
        if ($zip.Entries.Count -ne 1 -or $zip.Entries[0].FullName -cnotmatch '^OurOS-Pi5(?:-header)?-[^/\\]+\.img$' -or $zip.Entries[0].Length -ne 134217728) { throw "Invalid complete-image ZIP: $Path" }
        $stream=$zip.Entries[0].Open();$sha=[Security.Cryptography.SHA256]::Create()
        try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $sha.Dispose();$stream.Dispose() }
    } finally { $zip.Dispose() }
}
$name=[IO.Path]::GetFileName($source)
$destination=Get-ArtifactPath (Join-Path $archive ([IO.Path]::ChangeExtension($name,'.zip'))) $archive
$digest=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ((Test-Path -LiteralPath $destination) -and (Read-ZipDiskHash $destination) -eq $digest) {
    Write-Output "ZIP unchanged and decompressed SHA256 verified: $destination"
    return
}
$work=Get-ArtifactPath (Join-Path $build ('zip-stage-'+[guid]::NewGuid().ToString('N'))) $build
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    $temporary=Join-Path $work 'disk.zip'
    $zip=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry=$zip.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime=[DateTimeOffset]::new(2026,11,8,0,0,0,[TimeSpan]::Zero)
        $inputStream=[IO.File]::OpenRead($source);$outputStream=$entry.Open()
        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose();$inputStream.Dispose() }
    } finally { $zip.Dispose() }
    if ((Read-ZipDiskHash $temporary) -ne $digest) { throw 'Decompressed ZIP does not match the complete source image; previous ZIP retained' }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $digest) { throw 'Source changed during compression; previous ZIP retained' }
    if ((Get-Item -LiteralPath $temporary).Length -ge 100000000) { throw 'ZIP exceeds the publishing size limit; previous ZIP retained' }
    if (Test-Path -LiteralPath $destination) {
        $previous=Get-ArtifactPath (Join-Path $archive ([IO.Path]::GetFileNameWithoutExtension($name)+'-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')+'.zip')) $archive
        if (Test-Path -LiteralPath $previous) { throw 'ZIP backup filename collision; previous ZIP retained' }
        # Copy before promotion so even a failed promotion leaves the current ZIP.
        Copy-Item -LiteralPath $destination -Destination $previous
    }
    Copy-Item -LiteralPath $temporary -Destination $destination -Force
    if ((Read-ZipDiskHash $destination) -ne $digest) { throw 'Published ZIP verification failed' }
    Write-Output "ZIP: $destination ($((Get-Item -LiteralPath $destination).Length) bytes)"
    Write-Output "PASS: exactly one 128 MiB disk image; decompressed SHA256=$($digest.ToLowerInvariant())"
} finally {
    $checked=Get-ArtifactPath $work $build
    $null=@(Get-ArtifactFiles $checked)
    Remove-Item -LiteralPath $checked -Recurse -Force
}
