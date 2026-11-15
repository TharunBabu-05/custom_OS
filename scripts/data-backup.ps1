# OurOS bank-zero backup, independent host validation. Never opens a device.
. "$PSScriptRoot/data-geometry.ps1"
function Assert-BackupZero([byte[]]$Bytes,[int]$At,[int]$Count) {
    for ($i=$At;$i -lt $At+$Count;$i++) { if ($Bytes[$i]) { throw 'Nonzero reserved/unused backup bytes' } }
}
function Get-OurOSDataBackup {
    param([Parameter(Mandatory=$true)][string]$Path)
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($file.PSIsContainer -or $file.Length -notin 33792,132608,263680) { throw 'Invalid backup length (expected a supported bank header/payload)' }
    [byte[]]$b=[IO.File]::ReadAllBytes($file.FullName)
    $v=[BitConverter]::ToUInt32($b,8)
    if ($v -notin 1,2,3,4 -or [Text.Encoding]::ASCII.GetString($b,0,8) -cne ('OUROSBK'+$v)) { throw 'Unsupported backup format' }
    $geometry=Get-OurOSDataGeometry $v;$slots=$geometry.Files;$cap=$geometry.FileBytes
    $directory=$slots*64;$payload=$directory+$slots*$cap
    if ($b.Length -ne 512+$payload -or [BitConverter]::ToUInt32($b,12) -ne 0 -or
        [BitConverter]::ToUInt64($b,16) -eq 0 -or [BitConverter]::ToUInt32($b,24) -ne $payload) { throw 'Invalid backup identity/geometry' }
    Assert-BackupZero $b 28 4;Assert-BackupZero $b 96 416
    $sha=[Security.Cryptography.SHA256]::Create();$entries=@()
    try {
        foreach ($part in @(@(0,64,64),@(512,$payload,32))) {
            if ([Convert]::ToBase64String($sha.ComputeHash($b,$part[0],$part[1])) -cne [Convert]::ToBase64String($b[$part[2]..($part[2]+31)])) { throw 'Backup checksum mismatch' }
        }
        $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        for ($slot=0;$slot -lt $slots;$slot++) {
            $at=512+$slot*64;$data=512+$directory+$slot*$cap
            if (!$b[$at]) { Assert-BackupZero $b $at 64;Assert-BackupZero $b $data $cap;continue }
            $n=0
            while ($n -lt 32 -and $b[$at+$n]) {
                $c=$b[$at+$n];if ($c -le 32 -or $c -ge 127 -or $c -eq 47 -or $c -eq 92) { throw 'Invalid backup name' };$n++
            }
            if ($n -eq 32) { throw 'Unterminated backup name' }
            $name=[Text.Encoding]::ASCII.GetString($b,$at,$n)
            $parent=0;$kind=1
            if ($v -ge 3) {
                $kind=[BitConverter]::ToUInt32($b,$at+36);$parent=[BitConverter]::ToUInt32($b,$at+40)
                if ($kind -notin 1,2 -or $parent -gt $slots) { throw 'Invalid directory kind/parent' }
            }
            if ($name -in '.','..' -or !$names.Add(([string]$parent+':'+$name))) { throw 'Invalid/duplicate sibling name' }
            Assert-BackupZero $b ($at+$n) (32-$n)
            if ($v -ge 3) { Assert-BackupZero $b ($at+44) 20 } else { Assert-BackupZero $b ($at+36) 28 }
            $length=[BitConverter]::ToUInt32($b,$at+32);if ($length -gt $cap) { throw 'Backup file too large' }
            if ($kind -eq 2 -and $length) { throw 'Directory contains file data' }
            Assert-BackupZero $b ($data+$length) ($cap-$length)
            $entries+=[pscustomobject]@{Name=$name;Path=$name;Slot=$slot;Parent=$parent;Directory=($kind -eq 2);Bytes=$length;DataOffset=$data;SHA256=([BitConverter]::ToString($sha.ComputeHash($b,$data,$length))).Replace('-','').ToLowerInvariant()}
        }
        foreach ($entry in $entries) {
            $visited=[Collections.Generic.HashSet[int]]::new();$null=$visited.Add($entry.Slot);$parent=$entry.Parent;$parts=@($entry.Name)
            while ($parent) {
                if (!$visited.Add([int]$parent-1)) { throw 'Directory cycle in backup' }
                $ancestor=@($entries | Where-Object { $_.Slot -eq $parent-1 })
                if ($ancestor.Count -ne 1 -or !$ancestor[0].Directory) { throw 'Missing/non-directory backup parent' }
                $parts=@($ancestor[0].Name)+$parts;$parent=$ancestor[0].Parent
            }
            $entry.Path=$parts -join '/'
            if ($v -ge 3 -and $entry.Path.Length+6 -gt 255) { throw 'Backup path exceeds mounted VFS path limit' }
        }
        $digest=([BitConverter]::ToString($sha.ComputeHash($b))).Replace('-','').ToLowerInvariant()
    } finally { $sha.Dispose() }
    return [pscustomobject]@{Version=$v;Generation=[BitConverter]::ToUInt64($b,16);Entries=$entries;SHA256=$digest;Raw=$b}
}
function New-OurOSDataBackup {
    param([ValidateSet(1,2,3,4)][int]$Version=2,[object[]]$Files=@())
    $geometry=Get-OurOSDataGeometry $Version;$slots=$geometry.Files;$cap=$geometry.FileBytes
    if ($Files.Count -gt $slots) { throw 'Too many backup nodes' }
    [byte[]]$b=New-Object byte[] $geometry.BackupBytes
    [Text.Encoding]::ASCII.GetBytes('OUROSBK'+$Version).CopyTo($b,0)
    [BitConverter]::GetBytes([uint32]$Version).CopyTo($b,8);[BitConverter]::GetBytes([uint64]1).CopyTo($b,16)
    [BitConverter]::GetBytes([uint32]($b.Length-512)).CopyTo($b,24)
    $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $occupied=[Collections.Generic.HashSet[int]]::new();$entries=@();$next=0
    foreach ($item in $Files) {
        if ($null -ne $item.Slot) { $slot=[int]$item.Slot } else { while ($occupied.Contains($next)) { $next++ };$slot=$next }
        $parent=if ($null -eq $item.Parent) { 0 } else { [int]$item.Parent };$directory=[bool]$item.Directory
        if ($slot -lt 0 -or $slot -ge $slots -or !$occupied.Add($slot) -or $parent -lt 0 -or $parent -gt $slots -or
            ($Version -lt 3 -and ($directory -or $parent)) -or $item.Name -cnotmatch '^[!-~]{1,31}$' -or
            $item.Name.Contains('/') -or $item.Name.Contains('\') -or $item.Name -in '.','..' -or
            !$names.Add(([string]$parent+':'+$item.Name)) -or $item.Data.Length -gt $cap -or ($directory -and $item.Data.Length)) { throw 'Invalid backup node' }
        $at=512+$slot*64;[Text.Encoding]::ASCII.GetBytes($item.Name).CopyTo($b,$at)
        [BitConverter]::GetBytes([uint32]$item.Data.Length).CopyTo($b,$at+32)
        if ($Version -ge 3) {
            [BitConverter]::GetBytes([uint32]$(if ($directory) { 2 } else { 1 })).CopyTo($b,$at+36)
            [BitConverter]::GetBytes([uint32]$parent).CopyTo($b,$at+40)
        }
        if ($item.Data.Length) { $item.Data.CopyTo($b,512+$slots*64+$slot*$cap) }
        $entries+=[pscustomobject]@{Name=$item.Name;Slot=$slot;Parent=$parent;Directory=$directory}
    }
    foreach ($entry in $entries) {
        $seen=[Collections.Generic.HashSet[int]]::new();$null=$seen.Add($entry.Slot);$parent=$entry.Parent;$parts=@($entry.Name)
        while ($parent) {
            if (!$seen.Add($parent-1)) { throw 'Directory cycle in backup nodes' }
            $ancestor=@($entries | Where-Object { $_.Slot -eq $parent-1 })
            if ($ancestor.Count -ne 1 -or !$ancestor[0].Directory) { throw 'Missing/non-directory backup parent' }
            $parts=@($ancestor[0].Name)+$parts;$parent=$ancestor[0].Parent
        }
        if (($parts -join '/').Length+6 -gt 255) { throw 'Backup path exceeds mounted VFS path limit' }
    }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $sha.ComputeHash($b,512,$b.Length-512).CopyTo($b,32);$sha.ComputeHash($b,0,64).CopyTo($b,64) } finally { $sha.Dispose() }
    return ,$b
}
