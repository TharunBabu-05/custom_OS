[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/data-backup.ps1"
$work=Join-Path $ProjectRoot 'build/storage-tests';New-Item -ItemType Directory -Force $work | Out-Null
& $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/persist_host.c filesystem/persist.c drivers/storage/block.c lib/sha256.c -o build/storage-tests/test-backup.exe
if ($LASTEXITCODE -ne 0) { throw 'Backup test compilation failed' }
& ./build/storage-tests/test-backup.exe --export-prefix (Join-Path $work 'native-backup')
if ($LASTEXITCODE -ne 0) { throw 'Native backup tests failed' }
function Serial-Text([byte[]]$Bytes) {
    $s=[Text.StringBuilder]::new();$null=$s.AppendLine('monitor preamble');$null=$s.AppendLine('-----BEGIN OUROS DATA BACKUP V1-----')
    for ($at=0;$at -lt $Bytes.Length;$at+=32) { $hex=([BitConverter]::ToString($Bytes,$at,32)).Replace('-','').ToLowerInvariant();$null=$s.AppendLine(('D{0:x8}:{1}' -f $at,$hex)) }
    $h=[Security.Cryptography.SHA256]::Create()
    try { $hash=([BitConverter]::ToString($h.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() } finally { $h.Dispose() }
    $null=$s.AppendLine('OUROS-DATA-SHA256 '+$hash);$null=$s.AppendLine('-----END OUROS DATA BACKUP V1-----');$null=$s.AppendLine('monitor ready')
    return $s.ToString()
}
foreach ($version in 1,2) {
    $source=Join-Path $work "native-backup.v$version.bak";$native=Get-OurOSDataBackup $source
    if ($native.Version -ne $version -or $native.Entries.Count -ne 1 -or $native.Entries[0].Name -cne 'native.txt') { throw 'Native export decode mismatch' }
    $log=Join-Path $work "serial-v$version.log";[IO.File]::WriteAllText($log,(Serial-Text $native.Raw))
    $out=Join-Path $work ('converted-'+[guid]::NewGuid().ToString('N')+'.bak')
    & "$PSScriptRoot/convert-data-backup.ps1" -Log $log -Output $out
    if ((Get-OurOSDataBackup $out).SHA256 -cne $native.SHA256) { throw 'Serial conversion changed backup bytes' }
    $files=@(@{Name='host.txt';Data=[Text.Encoding]::ASCII.GetBytes('host-generated')},@{Name='empty';Data=(New-Object byte[] 0)})
    $hostPath=Join-Path $work "host-v$version.bak";[IO.File]::WriteAllBytes($hostPath,(New-OurOSDataBackup -Version $version -Files $files))
    $hostBackup=Get-OurOSDataBackup $hostPath
    & ./build/storage-tests/test-backup.exe --backup $hostPath
    if ($LASTEXITCODE -ne 0) { throw 'Guest/native restore rejected host backup' }
    if ($hostBackup.Entries.Count -ne 2) { throw 'Host fixture entry count differs' }
    $converted=Join-Path $work ('migrated-v'+$version+'-'+[guid]::NewGuid().ToString('N')+'.bak')
    & "$PSScriptRoot/migrate-data-backup.ps1" -InputBackup $source -Output $converted
    $v3=Get-OurOSDataBackup $converted
    if ($v3.Version -ne 3 -or $v3.Entries.Count -ne 1 -or $v3.Entries[0].SHA256 -cne $native.Entries[0].SHA256) { throw 'Migration mismatch' }
    & ./build/storage-tests/test-backup.exe --backup $converted
    if ($LASTEXITCODE -ne 0) { throw 'Native restore rejected migrated v3 archive' }
}
$tree=Get-OurOSDataBackup (Join-Path $work 'native-backup.v3.bak')
if ($tree.Version -ne 3 -or $tree.Entries.Count -ne 2 -or @($tree.Entries | Where-Object { $_.Path -ceq 'docs/native.txt' -and !$_.Directory }).Count -ne 1) { throw 'Native nested export decode mismatch' }
$treeLog=Join-Path $work 'serial-v3.log';[IO.File]::WriteAllText($treeLog,(Serial-Text $tree.Raw))
$treeOutput=Join-Path $work ('converted-v3-'+[guid]::NewGuid().ToString('N')+'.bak')
& "$PSScriptRoot/convert-data-backup.ps1" -Log $treeLog -Output $treeOutput
& ./build/storage-tests/test-backup.exe --backup $treeOutput
if ($LASTEXITCODE -ne 0) { throw 'Native nested restore failed' }
foreach ($case in 'self','cycle','missing','file-parent','kind','directory-data','long-path') {
    [byte[]]$bad=$tree.Raw.Clone()
    switch ($case) {
        'self' { [BitConverter]::GetBytes([uint32]1).CopyTo($bad,552) }
        'cycle' { [BitConverter]::GetBytes([uint32]2).CopyTo($bad,552);[BitConverter]::GetBytes([uint32]2).CopyTo($bad,612);[Array]::Clear($bad,608,4);[Array]::Clear($bad,9728,8192) }
        'missing' { [BitConverter]::GetBytes([uint32]16).CopyTo($bad,616) }
        'file-parent' { [BitConverter]::GetBytes([uint32]2).CopyTo($bad,552) }
        'kind' { [BitConverter]::GetBytes([uint32]99).CopyTo($bad,548) }
        'directory-data' { $bad[1536]=1 }
        'long-path' {
            [Array]::Clear($bad,512,$bad.Length-512)
            for ($i=0;$i -lt 16;$i++) {
                $at=512+$i*64;for ($j=0;$j -lt 31;$j++) { $bad[$at+$j]=65+$i }
                [BitConverter]::GetBytes([uint32]2).CopyTo($bad,$at+36);[BitConverter]::GetBytes([uint32]$i).CopyTo($bad,$at+40)
            }
        }
    }
    $h=[Security.Cryptography.SHA256]::Create()
    try { $h.ComputeHash($bad,512,$bad.Length-512).CopyTo($bad,32);$h.ComputeHash($bad,0,64).CopyTo($bad,64) } finally { $h.Dispose() }
    $path=Join-Path $work 'bad-tree.bak';[IO.File]::WriteAllBytes($path,$bad);$rejected=$false
    try { $null=Get-OurOSDataBackup $path } catch { $rejected=$true };if (!$rejected) { throw "Accepted invalid directory graph: $case" }
}
$valid=Get-OurOSDataBackup (Join-Path $work 'native-backup.v2.bak');$text=Serial-Text $valid.Raw
$badLogs=@($text.Replace('D00000020:','D00000000:'),$text.Replace('-----END OUROS DATA BACKUP V1-----',''),
    $text.Replace('D00000000:','D00000000:0'),($text+$text),$text.Replace('OUROS-DATA-SHA256 '+$valid.SHA256,('OUROS-DATA-SHA256 '+('0'*64))))
foreach ($badLog in $badLogs) {
    $log=Join-Path $work 'bad-export.log';[IO.File]::WriteAllText($log,$badLog);$out=Join-Path $work ('reject-'+[guid]::NewGuid().ToString('N')+'.bak')
    $rejected=$false;try { & "$PSScriptRoot/convert-data-backup.ps1" -Log $log -Output $out } catch { $rejected=$true }
    if (!$rejected -or (Test-Path -LiteralPath $out)) { throw 'Malformed serial backup was published' }
}
foreach ($offset in 0,8,12,16,24,28,64,96,512,548,1536) {
    [byte[]]$bad=$valid.Raw.Clone();$bad[$offset]=$bad[$offset] -bxor 1;$path=Join-Path $work 'bad-archive.bak';[IO.File]::WriteAllBytes($path,$bad)
    $rejected=$false;try { $null=Get-OurOSDataBackup $path } catch { $rejected=$true };if (!$rejected) { throw "Accepted corrupt backup at $offset" }
}
foreach ($kind in 'bad-name','duplicate','unused','too-large','unterminated','tail') {
    [byte[]]$bad=$valid.Raw.Clone()
    switch ($kind) {
        'bad-name' { $bad[512]=47 }
        'duplicate' { [Array]::Copy($bad,512,$bad,576,64) }
        'unused' { $bad[577]=65 }
        'too-large' { [BitConverter]::GetBytes([uint32]8193).CopyTo($bad,544) }
        'unterminated' { for ($i=512;$i -lt 544;$i++) { $bad[$i]=65 } }
        'tail' { $bad[1550]=1 }
    }
    $h=[Security.Cryptography.SHA256]::Create()
    try { $h.ComputeHash($bad,512,$bad.Length-512).CopyTo($bad,32);$h.ComputeHash($bad,0,64).CopyTo($bad,64) } finally { $h.Dispose() }
    $path=Join-Path $work 'bad-archive.bak';[IO.File]::WriteAllBytes($path,$bad)
    $rejected=$false;try { $null=Get-OurOSDataBackup $path } catch { $rejected=$true };if (!$rejected) { throw "Accepted checksummed malformed directory: $kind" }
}
$log=Join-Path $work 'serial-v2.log';$existing=Join-Path $work 'existing.bak';[IO.File]::WriteAllText($existing,'keep-me')
$rejected=$false;try { & "$PSScriptRoot/convert-data-backup.ps1" -Log $log -Output $existing } catch { $rejected=$true }
if (!$rejected -or [IO.File]::ReadAllText($existing) -cne 'keep-me') { throw 'Converter overwrote an existing file' }
$native4=Get-OurOSDataBackup (Join-Path $work 'native-backup.v4.bak')
$log4=Join-Path $work 'serial-v4.log';[IO.File]::WriteAllText($log4,(Serial-Text $native4.Raw))
$converted4=Join-Path $work ('converted-v4-'+[guid]::NewGuid().ToString('N')+'.bak')
& "$PSScriptRoot/convert-data-backup.ps1" -Log $log4 -Output $converted4
if ((Get-OurOSDataBackup $converted4).SHA256 -cne $native4.SHA256) { throw 'v4 serial conversion changed bytes' }
[byte[]]$large=New-Object byte[] 16384;for ($i=0;$i -lt $large.Length;$i++) { $large[$i]=[byte](($i*29+7)%256) }
$nested=@(@{Name='docs';Slot=5;Directory=$true;Data=(New-Object byte[] 0)},
    @{Name='child';Slot=1;Parent=6;Directory=$true;Data=(New-Object byte[] 0)},
    @{Name='same';Slot=9;Parent=2;Data=$large},@{Name='same';Slot=12;Parent=6;Data=[Text.Encoding]::ASCII.GetBytes('sibling')})
$host4=Join-Path $work 'host-v4.bak';[IO.File]::WriteAllBytes($host4,(New-OurOSDataBackup -Version 4 -Files $nested))
$decoded4=Get-OurOSDataBackup $host4
if (@($decoded4.Entries | Where-Object { $_.Path -ceq 'docs/child/same' -and $_.Bytes -eq 16384 -and $_.Slot -eq 9 }).Count -ne 1) { throw 'v4 nested payload decode failed' }
& ./build/storage-tests/test-backup.exe --backup $host4
if ($LASTEXITCODE -ne 0) { throw 'Native restore rejected 16 KiB host-generated v4 tree' }
# Explicit conversion preserves out-of-order slots, holes, sibling names and parents.
$nested[2].Data=[byte[]]$large[0..8191]
$tree3=Join-Path $work 'host-tree-v3.bak';[IO.File]::WriteAllBytes($tree3,(New-OurOSDataBackup -Version 3 -Files $nested))
foreach ($version in 1,2,3) {
    $oldPath=if ($version -eq 3) { $tree3 } else { Join-Path $work "native-backup.v$version.bak" }
    $before=(Get-FileHash -LiteralPath $oldPath -Algorithm SHA256).Hash
    $target4=Join-Path $work ('migrated-'+$version+'-to4-'+[guid]::NewGuid().ToString('N')+'.bak')
    & "$PSScriptRoot/migrate-data-backup.ps1" -InputBackup $oldPath -Output $target4 -TargetVersion 4
    $next=Get-OurOSDataBackup $target4
    if ($next.Version -ne 4 -or (Get-FileHash -LiteralPath $oldPath -Algorithm SHA256).Hash -cne $before) { throw 'v4 conversion changed source or target version' }
    & ./build/storage-tests/test-backup.exe --backup $target4
    if ($LASTEXITCODE -ne 0) { throw 'Native restore rejected converted v4 backup' }
}
Write-Output 'PASS: C/PowerShell v1-v4 interchange, explicit migration preserving nested slots/parents/bytes, 16 KiB archives, serial integrity and no-overwrite publication.'
$migrated5=Join-Path $work ('migrated-v5-'+[guid]::NewGuid().ToString('N')+'.bak')
& "$PSScriptRoot/migrate-data-backup.ps1" -InputBackup $host4 -Output $migrated5 -TargetVersion 5
$decoded5=Get-OurOSDataBackup $migrated5
if ($decoded5.Version -ne 5 -or $decoded5.Entries.Count -ne $decoded4.Entries.Count -or
    (Get-OurOSDataBackup $host4).SHA256 -cne $decoded4.SHA256) { throw 'v5 migration changed layout or source' }
foreach ($entry in $decoded4.Entries) {
    $match=@($decoded5.Entries | Where-Object { $_.Path -ceq $entry.Path -and $_.Slot -eq $entry.Slot -and $_.SHA256 -ceq $entry.SHA256 })
    if ($match.Count -ne 1) { throw 'v5 migration changed file identity' }
}
$log5=Join-Path $work 'serial-v5.log';[IO.File]::WriteAllText($log5,(Serial-Text $decoded5.Raw))
$converted5=Join-Path $work ('converted-v5-'+[guid]::NewGuid().ToString('N')+'.bak')
& "$PSScriptRoot/convert-data-backup.ps1" -Log $log5 -Output $converted5
if ((Get-OurOSDataBackup $converted5).SHA256 -cne $decoded5.SHA256) { throw 'v5 serial conversion changed the archive' }
Write-Output 'PASS: explicit v4-to-v5 conversion preserves every path, slot and file hash; v5 serial conversion retains exact bytes.'
