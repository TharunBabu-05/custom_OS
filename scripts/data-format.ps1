# Deterministic blank OurOS data partition. The guest never formats disks.
. "$PSScriptRoot/data-geometry.ps1"
function New-OurOSDataPartition {
    param([ValidateSet(1,2,3,4)][int]$Version=1)
    $geometry=Get-OurOSDataGeometry $Version;$files=$geometry.Files;$fileBytes=$geometry.FileBytes
    $directoryBytes=$files*64;$payloadBytes=$directoryBytes+$files*$fileBytes;$bankSectors=1+$payloadBytes/512
    [byte[]]$result=New-Object byte[] (2048*512)
    [byte[]]$header=New-Object byte[] 512
    [Text.Encoding]::ASCII.GetBytes('OUROSDAT').CopyTo($header,0)
    foreach ($item in @(@(8,$Version),@(12,512),@(16,2048),@(20,$bankSectors),@(24,$files),@(28,$fileBytes))) {
        [BitConverter]::GetBytes([uint32]$item[1]).CopyTo($header,[int]$item[0])
    }
    $digest=[Security.Cryptography.SHA256]::Create()
    try {
        $digest.ComputeHash($header,0,64).CopyTo($header,64);$header.CopyTo($result,0)
        [Array]::Clear($header,0,512)
        [Text.Encoding]::ASCII.GetBytes('OUROSBK'+$Version).CopyTo($header,0)
        [BitConverter]::GetBytes([uint32]$Version).CopyTo($header,8)
        [BitConverter]::GetBytes([uint64]1).CopyTo($header,16)
        [BitConverter]::GetBytes([uint32]$payloadBytes).CopyTo($header,24)
        $digest.ComputeHash((New-Object byte[] $payloadBytes)).CopyTo($header,32)
        $digest.ComputeHash($header,0,64).CopyTo($header,64);$header.CopyTo($result,512)
    } finally { $digest.Dispose() }
    return ,$result
}
