# All guest writes target regular copies beneath build/usb-tests. The delivery
# disk and its read-only boot partition are independently hashed after testing.
[CmdletBinding()]
param([string]$Image='image_files/OurOS-Pi5-header-experimental-sd.img')
. "$PSScriptRoot/windows-tools.ps1"
$source=(Resolve-Path -LiteralPath $Image).Path
$sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$out=Join-Path $ProjectRoot 'build/handoff-test'
$kernel=Join-Path $out 'kernel.bin'
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build handoff-test first' }
New-Item -ItemType Directory -Force (Join-Path $ProjectRoot 'build/usb-tests') | Out-Null
function Segment-Hash([string]$Path,[long]$Offset,[long]$Count) {
    $f=[IO.File]::OpenRead($Path);$hash=[Security.Cryptography.SHA256]::Create();$buffer=New-Object byte[] 65536
    try {
        $f.Position=$Offset
        while ($Count) {
            $n=$f.Read($buffer,0,[int][Math]::Min($Count,$buffer.Length));if (!$n) { throw 'Truncated disk' }
            $null=$hash.TransformBlock($buffer,0,$n,$buffer,0);$Count-=$n
        }
        $null=$hash.TransformFinalBlock((New-Object byte[] 0),0,0)
        return [Convert]::ToBase64String($hash.Hash)
    } finally { $f.Dispose();$hash.Dispose() }
}
function Run-Session([string]$Disk,[string]$Name,[string]$Cpu,[string]$Virt,[string[]]$Commands,[string[]]$Expected,[int]$TaskPasses) {
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $start.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device qemu-xhci,id=xhci,p3=0 -drive file=`"$Disk`",if=none,format=raw,readonly=off,id=filedisk -device usb-storage,bus=xhci.0,drive=filedisk -kernel `"$kernel`""
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $p=New-Object System.Diagnostics.Process;$p.StartInfo=$start;$started=$false
    try {
        if (!$p.Start()) { throw 'Cannot start QEMU' };$started=$true
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 1000
        foreach ($line in $Commands) {
            foreach ($c in ($line+"`r`n").ToCharArray()) { $p.StandardInput.Write($c);$p.StandardInput.Flush();Start-Sleep -Milliseconds 6 }
            $delay=if ($line -eq 'taskiotest' -or $line -eq 'storagetest' -or $line.StartsWith('fwrite ') -or $line.StartsWith('fopen /data/')) { 1500 } else { 450 }
            Start-Sleep -Milliseconds $delay
        }
        Start-Sleep -Milliseconds 400
        if (!$p.HasExited) { $p.Kill() };$p.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$Name.log")
        $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
        foreach ($fragment in $Expected) { if (!$text.Contains($fragment)) { throw "$Name missing '$fragment'; see $out/$Name.log" } }
        $taskPass='PASS: three tasks kept independent owned file cursors across yields/sleeps; foreign access rejected, six handles auto-closed and memory restored.'
        if ([regex]::Matches($text,[regex]::Escape($taskPass)).Count -ne $TaskPasses) { throw "$Name task I/O pass count mismatch; see log" }
        if ($text.Contains('FATAL') -or $text.Contains('FAIL:') -or $text.Contains('fault=yes') -or $text.Contains('Unknown command.')) { throw "$Name unexpected command/task/memory/IRQ failure; see log" }
        Write-Output "PASS: $Name"
    } finally { if ($started -and !$p.HasExited) { $p.Kill();$p.WaitForExit() };$p.Dispose() }
}
$filePass='PASS: owned file handles, independent cursors, seek/overwrite, stale/foreign rejection, path pins and owner cleanup; memory restored.'
$expectedContent='saved-XX-file-handle'
$prefixHash=Segment-Hash $source 0 (260097L*512) # Includes data superblock.
$tailOffset=(260096L+133)*512
$tailHash=Segment-Hash $source $tailOffset (134217728L-$tailOffset)
foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
    $stem="files-$($case[0])-$($case[1])"
    $disk=Join-Path $ProjectRoot ('build/usb-tests/kfile-'+$case[0]+'-'+$case[1]+'-'+[guid]::NewGuid().ToString('N')+'.img')
    Copy-Item -LiteralPath $source -Destination $disk
    $create=@('storageinit','datainit','fileinfo','fopen /data/fd-note.txt w','fwrite 16 saved-by-file-handle','fseek 16 0 set','fread 16 20',
        'write /data//fd-note.txt refused','rm /data/./fd-note.txt','fclose 16','fopen /data/fd-note.txt rw','fseek 32 6 set','fwrite 32 XX',
        'fseek 32 0 set','fread 32 64','fclose 32','fopen /boot/CONFIG.TXT r','fread 48 16',
        'mmuinit','irqinit','timerstart','taskiotest','taskiotest','filetest','fileinfo','fclose 48','fread 48 1',
        'sync','datainfo','fileinfo','heaptest','pagetest','heapinfo','timerinfo','timerstop','echo files-created','halt')
    Run-Session $disk "$stem-create" $case[0] $case[1] $create @('Opened handle=16','Opened handle=32','Opened handle=48',
        'Written bytes=20','Written bytes=2','file: file access mode denied','fs: root or current directory is busy',
        "`r`n$expectedContent`r`n",'handle=48 owner=1 mode=r offset=16 bytes=','path=/boot/CONFIG.TXT',
        'file: invalid or foreign file handle',$filePass,'generation=4','Open files: 0/16','pages=2 objects=3 capacity_bytes=544','files-created') 2
    # A fresh process resets the handle table and owner identities. The disk
    # file is reopened without reflashing or copying from the seed image.
    $reopen=@('storageinit','datainit','datainfo','fileinfo','fopen /data/fd-note.txt r','fread 16 64',
        'fseek 16 -6 end','fread 16 64','fseek 16 0 set','fread 16 64','fclose 16','fread 16 1',
        'fopen /boot/CONFIG.TXT w','fopen /boot/new.txt w','write /boot/CONFIG.TXT refused',
        'mmuinit','irqinit','timerstart','filetest','taskiotest','fileinfo','sync','heaptest','pagetest','heapinfo',
        'timerinfo','timerstop','echo files-reopened','halt')
    Run-Session $disk "$stem-reopen" $case[0] $case[1] $reopen @('Opened handle=16',"`r`n$expectedContent`r`n",'Read bytes=20',
        "`r`nhandle`r`n",'Read bytes=6','File offset=14','file: invalid or foreign file handle','file: read-only filesystem',
        'fs: read-only filesystem',$filePass,'generation=4','Open files: 0/16','pages=2 objects=3 capacity_bytes=544','files-reopened') 1
    if ((Segment-Hash $disk 0 (260097L*512)) -ne $prefixHash -or (Segment-Hash $disk $tailOffset (134217728L-$tailOffset)) -ne $tailHash) {
        throw 'Guest modified MBR/boot files/data superblock or unused sectors outside the two data banks'
    }
    # Decode independently of the guest implementation: validate both committed
    # bank identities and SHA256 digests, then inspect the newest generation.
    $f=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
    try { $f.Position=260096L*512;if ($f.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Truncated data partition' } } finally { $f.Dispose() }
    $hash=[Security.Cryptography.SHA256]::Create();$chosen=-1;$generation=0L
    try {
        foreach ($bank in 0,1) {
            $at=512+$bank*66*512
            if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne 'OUROSBK1' -or [BitConverter]::ToUInt32($bytes,$at+8) -ne 1 -or
                [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt32($bytes,$at+24) -ne 33280 -or
                [Convert]::ToBase64String($hash.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                [Convert]::ToBase64String($hash.ComputeHash($bytes,$at+512,33280)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) {
                throw 'Independent snapshot header/payload checksum or identity failed'
            }
            $g=[BitConverter]::ToUInt64($bytes,$at+16);if ($g -gt $generation) { $generation=$g;$chosen=$at+512 }
        }
        if ($generation -ne 4 -or [Text.Encoding]::ASCII.GetString($bytes,$chosen,32).TrimEnd([char]0) -cne 'fd-note.txt' -or
            [BitConverter]::ToUInt32($bytes,$chosen+32) -ne $expectedContent.Length -or
            [Text.Encoding]::ASCII.GetString($bytes,$chosen+512,$expectedContent.Length) -cne $expectedContent) {
            throw 'Independent persistent handle content/length/generation check failed'
        }
        foreach ($i in 1..7) { if ($bytes[$chosen+$i*64]) { throw 'Unexpected persistent file was created' } }
    } finally { $hash.Dispose() }
    Write-Output "PASS: $stem owned handle writes/seek survive cold boot; monitor handle survives repeated task I/O/cleanup, read-only boot enforced, independent generation/content/hash checks and unchanged boot/unused sectors."
}
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Source delivery image changed' }
Write-Output 'PASS: source delivery image unchanged; writes were restricted to regular build/usb-tests/kfile-*.img copies.'
