# Complete flashable disk images only. image_files contains ordinary .img files.
function Get-ArtifactPath([string]$Path,[string]$Root) {
    $full=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $boundary=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if (!$full.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw "Artifact path escapes its permitted root: $full" }
    $walk=$full
    while ($walk) {
        if (Test-Path -LiteralPath $walk) {
            if ((Get-Item -LiteralPath $walk -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point is not permitted: $walk" }
        }
        $parent=[IO.Path]::GetDirectoryName($walk)
        if ($parent -eq $walk) { break };$walk=$parent
    }
    return $full
}
function Get-ArtifactFiles([string]$Directory) {
    if (!(Test-Path -LiteralPath $Directory)) { return }
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in generated tree: $($item.FullName)" }
        if ($item.PSIsContainer) { Get-ArtifactFiles $item.FullName } else { $item }
    }
}
function Test-CompleteDiskImage([string]$Image) {
    if (!(Test-Path -LiteralPath $Image -PathType Leaf) -or (Get-Item -LiteralPath $Image).Length -ne 134217728) { return $false }
    $stream=[IO.File]::OpenRead($Image)
    try {
        $mbr=New-Object byte[] 512
        if ($stream.Read($mbr,0,512) -ne 512) { return $false }
        if ($mbr[510] -ne 85 -or $mbr[511] -ne 170 -or $mbr[450] -ne 12 -or [BitConverter]::ToUInt32($mbr,454) -ne 2048) { return $false }
        $stream.Position=1048576;$boot=New-Object byte[] 512
        if ($stream.Read($boot,0,512) -ne 512) { return $false }
        return $boot[510] -eq 85 -and $boot[511] -eq 170 -and [Text.Encoding]::ASCII.GetString($boot,82,8) -eq 'FAT32   '
    } finally { $stream.Dispose() }
}
function Save-DiskImageBackup([string]$Image,[string]$BuildDirectory,[string]$ArchiveDirectory) {
    $source=Get-ArtifactPath $Image $BuildDirectory
    $name=[IO.Path]::GetFileName($source)
    if ($name -cnotmatch '^OurOS-Pi5(?:-header)?-.+\.img$' -or !(Test-CompleteDiskImage $source)) { throw "Not a complete named Pi 5 disk image: $source" }
    $destination=Get-ArtifactPath (Join-Path $ArchiveDirectory $name) $ArchiveDirectory
    $digest=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $digest) { return $destination }
        # Keep the previous complete image under a readable date/time filename.
        $stem=[IO.Path]::GetFileNameWithoutExtension($name)
        $version=Get-ArtifactPath (Join-Path $ArchiveDirectory ($stem+'-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')+'.img')) $ArchiveDirectory
        if (Test-Path -LiteralPath $version) { throw 'Version filename collision; existing images retained' }
        if ($name -match '-experimental-sd\.img$') {
            Move-Item -LiteralPath $destination -Destination $version
        } else { $destination=$version }
    }
    Copy-Item -LiteralPath $source -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $digest) { throw 'Disk image backup hash mismatch; source retained' }
    return $destination
}
