# Deterministic blank OurOS data v1 partition. The guest never formats disks.
function New-OurOSDataPartition {
    [byte[]]$result=New-Object byte[] (2048*512)
    [byte[]]$header=New-Object byte[] 512
    [Text.Encoding]::ASCII.GetBytes('OUROSDAT').CopyTo($header,0)
    foreach ($item in @(@(8,1),@(12,512),@(16,2048),@(20,66),@(24,8),@(28,4096))) {
        [BitConverter]::GetBytes([uint32]$item[1]).CopyTo($header,[int]$item[0])
    }
    $digest=[Security.Cryptography.SHA256]::Create()
    try {
        $digest.ComputeHash($header,0,64).CopyTo($header,64);$header.CopyTo($result,0)
        [Array]::Clear($header,0,512)
        [Text.Encoding]::ASCII.GetBytes('OUROSBK1').CopyTo($header,0)
        [BitConverter]::GetBytes([uint32]1).CopyTo($header,8)
        [BitConverter]::GetBytes([uint64]1).CopyTo($header,16)
        [BitConverter]::GetBytes([uint32]33280).CopyTo($header,24)
        $digest.ComputeHash((New-Object byte[] 33280)).CopyTo($header,32)
        $digest.ComputeHash($header,0,64).CopyTo($header,64);$header.CopyTo($result,512)
    } finally { $digest.Dispose() }
    return ,$result
}
