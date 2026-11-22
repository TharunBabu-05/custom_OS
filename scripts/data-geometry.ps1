# Canonical host geometry; native guest tests validate independently encoded fixtures.
function Get-OurOSDataGeometry {
    param([ValidateSet(1,2,3,4,5)][int]$Version)
    $slots=if ($Version -eq 1) { 8 } elseif ($Version -eq 5) { 32 } else { 16 }
    $cap=if ($Version -eq 1) { 4096 } elseif ($Version -eq 4) { 16384 } elseif ($Version -eq 5) { 32768 } else { 8192 }
    $directory=$slots*64;$archive=$directory+$slots*$cap
    $payload=if ($Version -eq 5) { 2*$directory } else { $archive }
    $bank=1+$payload/512
    if (1+2*$bank -gt 2048) { throw 'Data geometry exceeds its fixed partition' }
    $magic=if ($Version -eq 5) { 'OUROSEX5' } else { 'OUROSBK'+$Version }
    return [pscustomobject]@{Version=$Version;Files=$slots;FileBytes=$cap;DirectoryBytes=$directory;
        PayloadBytes=$payload;BankSectors=$bank;FirstExtent=(1+2*$bank);BackupBytes=(512+$archive);Magic=$magic}
}
