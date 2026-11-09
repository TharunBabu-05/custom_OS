[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$scratch=Join-Path $project ('build/archive-tests/'+[guid]::NewGuid().ToString('N'))
$build=Join-Path $scratch 'build';$archive=Join-Path $scratch 'image_files'
$profile=Join-Path $build 'rpi5-header'
$stage=Join-Path $profile ('disk-stage-'+('a'*32))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
function Require([bool]$condition,[string]$message) { if (!$condition) { throw $message } }
function Must-Fail([scriptblock]$action) { $failed=$false;try { & $action | Out-Null } catch { $failed=$true };Require $failed 'Expected rejection' }
$current=Join-Path $profile 'OurOS-Pi5-header-experimental-sd.img'
$old=Join-Path $profile 'OurOS-Pi5-header-ramfs-verified.img'
try {
    # Format fixture: regular file, correct disk length/MBR/FAT32 signatures.
    $stream=[IO.File]::Open($current,[IO.FileMode]::CreateNew)
    try {
        $stream.SetLength(134217728);$mbr=New-Object byte[] 512
        $mbr[510]=85;$mbr[511]=170;$mbr[450]=12;[BitConverter]::GetBytes([uint32]2048).CopyTo($mbr,454)
        $stream.Write($mbr,0,512);$stream.Position=1048576;$boot=New-Object byte[] 512
        $boot[510]=85;$boot[511]=170;[Text.Encoding]::ASCII.GetBytes('FAT32   ').CopyTo($boot,82);$stream.Write($boot,0,512)
    } finally { $stream.Dispose() }
    Copy-Item -LiteralPath $current -Destination $old
    $originalHash=(Get-FileHash $current).Hash
    [IO.File]::WriteAllBytes((Join-Path $profile 'ouros-rpi5.img'),[byte[]](1,2,3))
    [IO.File]::WriteAllBytes((Join-Path $stage 'OUROS.IMG'),[byte[]](1,2,3))
    'keep' | Set-Content (Join-Path $profile 'kernel.elf')
    'keep' | Set-Content (Join-Path $build 'unknown.txt')
    & "$PSScriptRoot/archive-images.ps1" -BuildDirectory $build -ArchiveDirectory $archive
    $saved=Join-Path $archive ([IO.Path]::GetFileName($current))
    Require ((Get-FileHash $saved).Hash -eq $originalHash) 'Backup differs from source'
    Require (@(Get-ChildItem $archive -File).Count -eq 2 -and @(Get-ChildItem $archive -Directory).Count -eq 0) 'Expected flat disk images only'
    & "$PSScriptRoot/archive-images.ps1" -BuildDirectory $build -ArchiveDirectory $archive
    Require (@(Get-ChildItem $archive -File).Count -eq 2) 'Unchanged build created duplicate images'
    $stream=[IO.File]::OpenWrite($current)
    try { $stream.Position=1024;$stream.WriteByte(42) } finally { $stream.Dispose() }
    & "$PSScriptRoot/archive-images.ps1" -Image $current -BuildDirectory $build -ArchiveDirectory $archive
    $previous=@(Get-ChildItem $archive -File | Where-Object { $_.Name -like 'OurOS-Pi5-header-experimental-sd-*.img' })
    Require ($previous.Count -eq 1 -and (Get-FileHash $previous[0].FullName).Hash -eq $originalHash) 'Previous image was not preserved under a dated name'
    Require ((Get-FileHash $saved).Hash -eq (Get-FileHash $current).Hash) 'Latest image was not updated'
    Must-Fail { & "$PSScriptRoot/archive-images.ps1" -Image (Join-Path $profile 'ouros-rpi5.img') -BuildDirectory $build -ArchiveDirectory $archive }
    Must-Fail { & "$PSScriptRoot/clean-build.ps1" -BuildDirectory (Join-Path $project 'docs') -ArchiveDirectory $archive }
    Must-Fail { & "$PSScriptRoot/archive-images.ps1" -BuildDirectory $build -ArchiveDirectory $build }
    & "$PSScriptRoot/clean-build.ps1" -BuildDirectory $build -ArchiveDirectory $archive
    Require (!(Test-Path $stage) -and !(Test-Path $old)) 'Old generated files remain'
    Require ((Test-Path $current) -and (Test-Path (Join-Path $profile 'kernel.elf')) -and (Test-Path (Join-Path $build 'unknown.txt'))) 'Retained file was removed'
    Require (@(Get-ChildItem $archive -Directory).Count -eq 0 -and @(Get-ChildItem $archive -File | Where-Object { $_.Extension -ne '.img' }).Count -eq 0) 'Extra archive folders or files were created'
    & "$PSScriptRoot/compress-image.ps1" -Image $current -ArchiveDirectory $archive
    $zipPath=[IO.Path]::ChangeExtension($saved,'.zip')
    $zipHash=(Get-FileHash $zipPath).Hash
    & "$PSScriptRoot/compress-image.ps1" -Image $current -ArchiveDirectory $archive
    Require (@(Get-ChildItem $archive -Filter *.zip).Count -eq 1 -and (Get-FileHash $zipPath).Hash -eq $zipHash) 'Unchanged disk duplicated or rewrote ZIP'
    $stream=[IO.File]::OpenWrite($current)
    try { $stream.Position=1024;$stream.WriteByte(43) } finally { $stream.Dispose() }
    & "$PSScriptRoot/compress-image.ps1" -Image $current -ArchiveDirectory $archive
    $oldZip=@(Get-ChildItem $archive -Filter '*-experimental-sd-*.zip')
    Require ($oldZip.Count -eq 1 -and (Get-FileHash $oldZip[0].FullName).Hash -eq $zipHash) 'Previous ZIP was not preserved byte-for-byte'
    $zip=[IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        Require ($zip.Entries.Count -eq 1 -and $zip.Entries[0].FullName -eq [IO.Path]::GetFileName($current) -and $zip.Entries[0].Length -eq 134217728) 'ZIP is not one complete disk'
        $extracted=Join-Path $scratch 'extracted.img'
        [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.Entries[0],$extracted)
    } finally { $zip.Dispose() }
    Require ((Get-FileHash $extracted).Hash -eq (Get-FileHash $current).Hash) 'Independent extraction hash mismatch'
    Must-Fail { & "$PSScriptRoot/compress-image.ps1" -Image (Join-Path $profile 'ouros-rpi5.img') -ArchiveDirectory $archive }
    Must-Fail { & "$PSScriptRoot/compress-image.ps1" -Image $current -ArchiveDirectory (Join-Path $project 'docs') }
    Write-Output 'PASS: full-disk ZIP, independent extraction/hash, unchanged reuse, dated ZIP preservation, raw-kernel and output-path rejection.'
    Write-Output 'PASS: flat complete disk images only, original phase names, unchanged-image reuse, dated previous versions, raw-kernel refusal and safe cleanup.'
} finally {
    $resolved=Get-ArtifactPath $scratch (Join-Path $project 'build')
    $null=@(Get-ArtifactFiles $resolved)
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
