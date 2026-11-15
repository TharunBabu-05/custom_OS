# Canonical host geometry; native guest tests validate independently encoded fixtures.
function Get-OurOSDataGeometry {
    param([ValidateSet(1,2,3,4)][int]$Version)
    $slots=if ($Version -eq 1) { 8 } else { 16 }
    $cap=if ($Version -eq 1) { 4096 } elseif ($Version -eq 4) { 16384 } else { 8192 }
    $directory=$slots*64;$payload=$directory+$slots*$cap;$bank=1+$payload/512
    if (1+2*$bank -gt 2048) { throw 'Data geometry exceeds its fixed partition' }
    return [pscustomobject]@{Version=$Version;Files=$slots;FileBytes=$cap;DirectoryBytes=$directory;
        PayloadBytes=$payload;BankSectors=$bank;UsedSectors=(1+2*$bank);BackupBytes=(512+$payload)}
}
