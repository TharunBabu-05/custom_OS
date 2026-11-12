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
        while ($Count) { $n=$f.Read($buffer,0,[int][Math]::Min($Count,$buffer.Length));if (!$n) { throw 'Truncated disk' };$null=$hash.TransformBlock($buffer,0,$n,$buffer,0);$Count-=$n }
        $null=$hash.TransformFinalBlock((New-Object byte[] 0),0,0)
        return [Convert]::ToBase64String($hash.Hash)
    } finally { $f.Dispose();$hash.Dispose() }
}
function Run-Session([string]$Disk,[string]$Name,[string]$Cpu,[string]$Virt,[string[]]$Commands,[string[]]$Expected,[switch]$ReadOnly) {
    $ro=if ($ReadOnly) { 'on' } else { 'off' }
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $start.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device qemu-xhci,id=xhci,p3=0 -drive file=`"$Disk`",if=none,format=raw,readonly=$ro,id=datadisk -device usb-storage,bus=xhci.0,drive=datadisk -kernel `"$kernel`""
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $p=New-Object System.Diagnostics.Process;$p.StartInfo=$start;$started=$false
    try {
        if (!$p.Start()) { throw 'Cannot start QEMU' };$started=$true;$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 1000
        foreach ($line in $Commands) {
            foreach ($c in ($line+"`r`n").ToCharArray()) { $p.StandardInput.Write($c);$p.StandardInput.Flush();Start-Sleep -Milliseconds 6 }
            Start-Sleep -Milliseconds $(if ($line -eq 'schedtest' -or $line -eq 'timertest' -or $line -eq 'storagetest' -or $line.StartsWith('write /data/') -or $line.StartsWith('rm /data/')) { 1300 } else { 450 })
        }
        Start-Sleep -Milliseconds 400;if (!$p.HasExited) { $p.Kill() };$p.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$Name.log");$stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
        foreach ($fragment in $Expected) { if (!$text.Contains($fragment)) { throw "$Name missing '$fragment'; see $out/$Name.log" } }
        if ($text.Contains('FATAL') -or $text.Contains('FAIL:') -or $text.Contains('fault=yes')) { throw "$Name unexpected task/memory/IRQ failure; see log" }
        Write-Output "PASS: $Name"
    } finally { if ($started -and !$p.HasExited) { $p.Kill();$p.WaitForExit() };$p.Dispose() }
}
$boot=@('mmuinit','irqinit','timerstart','storageinit','datainit')
$taskPass='PASS: three guarded kernel tasks yielded/slept/exited; callee registers/stack/heap data preserved; PMM/heap counts restored.'
$prefixHash=Segment-Hash $source 0 (260096L*512)
$tailOffset=(260096L+133)*512
$tailHash=Segment-Hash $source $tailOffset (134217728L-$tailOffset)
foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
    $stem="data-$($case[0])-$($case[1])"
    $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$stem+'-'+[guid]::NewGuid().ToString('N')+'.img')
    Copy-Item -LiteralPath $source -Destination $disk
    Run-Session $disk "$stem-create" $case[0] $case[1] ($boot+@('datainfo','write /data/hello.txt persistent-after-reboot','touch /data/empty','cat /data/hello.txt','ls /data','write /tmp/volatile.txt gone','write /boot/CONFIG.TXT blocked','sync','schedtest','taskinfo','timertest','storagetest','echo persistence-created','halt')) @('/data: OurOS snapshot v1',"`r`npersistent-after-reboot`r`n",'f 0 empty','fs: read-only filesystem',$taskPass,'last_result=passed','persistence-created')
    Run-Session $disk "$stem-reopen" $case[0] $case[1] ($boot+@('cat /data/hello.txt','cat /tmp/volatile.txt','write /data/hello.txt changed-v2','rm /data/empty','sync','datainfo','schedtest','timerstop','echo persistence-reopened','halt')) @("`r`npersistent-after-reboot`r`n",'fs: path not found','generation=5',$taskPass,'persistence-reopened')
    Run-Session $disk "$stem-final" $case[0] $case[1] ($boot+@('cat /data/hello.txt','cat /data/empty','touch /data/hello.txt','datainit','ls /data/hello.txt/..','mkdir /data/folder','rmdir /data','datainfo','sync','heaptest','pagetest','mmutest','schedtest','heapinfo','timerinfo','timerstop','echo persistence-final','halt')) @("`r`nchanged-v2`r`n",'fs: path not found','fs: not a directory','fs: unsupported filesystem format','fs: root or current directory is busy','generation=5',$taskPass,'pages=2 objects=3 capacity_bytes=544','persistence-final')
    if ((Segment-Hash $disk 0 (260096L*512)) -ne $prefixHash -or (Segment-Hash $disk $tailOffset (134217728L-$tailOffset)) -ne $tailHash) { throw 'Guest changed MBR/boot files or sectors outside data banks' }
    # Independent host decoder checks committed bytes, not just guest messages.
    $f=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
    try { $f.Position=260096L*512;if ($f.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Truncated data' } } finally { $f.Dispose() }
    $hash=[Security.Cryptography.SHA256]::Create();$chosen=-1;$generation=0L
    try {
        foreach ($bank in 0,1) {
            $at=512+$bank*66*512
            if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne 'OUROSBK1' -or [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or
                [Convert]::ToBase64String($hash.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                [Convert]::ToBase64String($hash.ComputeHash($bytes,$at+512,33280)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Independent snapshot checksum/identity failed' }
            $g=[BitConverter]::ToUInt64($bytes,$at+16);if ($g -gt $generation) { $generation=$g;$chosen=$at+512 }
        }
        if ($generation -ne 5 -or [Text.Encoding]::ASCII.GetString($bytes,$chosen,32).TrimEnd([char]0) -cne 'hello.txt' -or
            [BitConverter]::ToUInt32($bytes,$chosen+32) -ne 10 -or [Text.Encoding]::ASCII.GetString($bytes,$chosen+512,10) -cne 'changed-v2') { throw 'Independent persisted namespace/content failed' }
        foreach ($i in 1..7) { if ($bytes[$chosen+$i*64]) { throw 'Removed/unused file survived' } }
    } finally { $hash.Dispose() }
    $savedHash=(Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash
    Run-Session $disk "$stem-readonly" $case[0] $case[1] ($boot+@('write /data/no.txt refused','datainfo','echo readonly-alive','halt')) @('fs: storage I/O error','writes=failed','readonly-alive') -ReadOnly
    if ((Get-FileHash -LiteralPath $disk -Algorithm SHA256).Hash -ne $savedHash) { throw 'Read-only device changed' }
    Write-Output "PASS: $stem three cold boots, synchronous USB2 writes/flush, independent SHA256/content generation checks, unchanged boot/MBR/unused sectors, IRQ/tasks and read-only media failure."
}
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Source delivery image changed' }
Write-Output 'PASS: source delivery image unchanged; only regular build/usb-tests copies were written.'
