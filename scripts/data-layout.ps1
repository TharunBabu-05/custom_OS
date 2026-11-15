# Read only the two supported data layouts; callers decode bank contents independently.
function Get-OurOSDataLayout([string]$Image) {
    $stream=[IO.File]::OpenRead((Resolve-Path -LiteralPath $Image).Path)
    try {
        $super=New-Object byte[] 512;$stream.Position=260096L*512
        if ($stream.Read($super,0,512) -ne 512) { throw 'Short data superblock' }
    } finally { $stream.Dispose() }
    $version=[BitConverter]::ToUInt32($super,8)
    if ([Text.Encoding]::ASCII.GetString($super,0,8) -cne 'OUROSDAT' -or $version -notin @(1,2,3)) { throw 'Unsupported data format' }
    $files=if ($version -eq 1) { 8 } else { 16 };$fileBytes=if ($version -eq 1) { 4096 } else { 8192 }
    $directory=$files*64;$payload=$directory+$files*$fileBytes;$bank=1+$payload/512
    if ([BitConverter]::ToUInt32($super,12) -ne 512 -or [BitConverter]::ToUInt32($super,16) -ne 2048 -or
        [BitConverter]::ToUInt32($super,20) -ne $bank -or [BitConverter]::ToUInt32($super,24) -ne $files -or [BitConverter]::ToUInt32($super,28) -ne $fileBytes) { throw 'Inconsistent data geometry' }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { if ([Convert]::ToBase64String($sha.ComputeHash($super,0,64)) -cne [Convert]::ToBase64String($super[64..95])) { throw 'Data superblock checksum mismatch' } } finally { $sha.Dispose() }
    return [pscustomobject]@{ Version=$version;Files=$files;FileBytes=$fileBytes;DirectoryBytes=$directory;PayloadBytes=$payload;BankSectors=$bank;UsedSectors=1+2*$bank;Magic=('OUROSBK'+$version) }
}
