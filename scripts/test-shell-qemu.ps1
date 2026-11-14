# Interactive acceptance against regular scratch files only. No physical drive.
[CmdletBinding()]
param([string]$Image='image_files/OurOS-Pi5-header-experimental-sd.img',[switch]$Smoke,[switch]$Stdio)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
$source=(Resolve-Path -LiteralPath $Image).Path
$original=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$contentHasher=[Security.Cryptography.SHA256]::Create()
try { $contentHash=([BitConverter]::ToString($contentHasher.ComputeHash([Text.Encoding]::ASCII.GetBytes('persistent shell data')))).Replace('-','').ToLowerInvariant() } finally { $contentHasher.Dispose() }
$out=Join-Path $ProjectRoot 'build/handoff-test'
$kernel=Join-Path $out 'kernel.bin'
New-Item -ItemType Directory -Force (Join-Path $ProjectRoot 'build/usb-tests') | Out-Null
function Range-Hash([string]$Path,[long]$Offset,[long]$Length) {
    $f=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create();$b=New-Object byte[] 65536
    try {
        $f.Position=$Offset
        while ($Length) { $n=$f.Read($b,0,[int][Math]::Min($Length,$b.Length));if (!$n) { throw 'Short disk' };$null=$sha.TransformBlock($b,0,$n,$b,0);$Length-=$n }
        $null=$sha.TransformFinalBlock((New-Object byte[] 0),0,0);return [Convert]::ToBase64String($sha.Hash)
    } finally { $f.Dispose();$sha.Dispose() }
}
function Session([string]$Disk,[string]$Name,[string]$Cpu,[string]$Virt,[object[]]$Actions,[string[]]$Expected,[int]$Restores) {
    $si=New-Object Diagnostics.ProcessStartInfo
    $si.FileName=$QemuExe
    $si.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device qemu-xhci,id=xhci,p3=0 -drive file=`"$Disk`",if=none,format=raw,readonly=off,id=shelldisk -device usb-storage,bus=xhci.0,drive=shelldisk -kernel `"$kernel`""
    $si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardInput=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
    $p=New-Object Diagnostics.Process;$p.StartInfo=$si;$started=$false
    try {
        if (!$p.Start()) { throw 'Cannot start QEMU' };$started=$true
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 1000
        foreach ($action in $Actions) {
            $delay=550;$raw=$false;$text=$action
            if ($action -is [hashtable]) { $text=$action.Text;$delay=$action.Delay;$raw=$action.Raw }
            if (!$raw) { $text+="`r`n" }
            foreach ($c in $text.ToCharArray()) { $p.StandardInput.Write($c);$p.StandardInput.Flush();Start-Sleep -Milliseconds 6 }
            Start-Sleep -Milliseconds $delay
        }
        if (!$p.HasExited) { $p.Kill() };$p.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$Name.log")
        $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
        foreach ($s in $Expected) { if (!$text.Contains($s)) { throw "$Name missing '$s'; see log" } }
        if ($text.Contains('FATAL') -or $text.Contains('FAIL:') -or $text.Contains('Unknown command.') -or $text.Contains('fault=yes')) { throw "$Name unexpected failure" }
        if ($text.Contains('PASS: child readline before parent wait') -and
            ![regex]::IsMatch($text,'TTYCHK ready: enter terminal-ok\r?\nParent now waits for the early terminal reader\.')) { throw "$Name did not force a child read before parent wait" }
        if ([regex]::Matches($text,[regex]::Escape('App resources restored; monitor ready.')).Count -ne $Restores) { throw "$Name cleanup count differs" }
        if (![regex]::IsMatch($text,'> pwd\r?\n/\r?\n')) { throw "$Name shell changed monitor cwd" }
        Write-Output "PASS: $Name"
    } finally { if ($started -and !$p.HasExited) { $p.Kill();$p.WaitForExit() };$p.Dispose() }
}
if ($Stdio) {
    function Digest([byte[]]$Bytes) {
        $hasher=[Security.Cryptography.SHA256]::Create()
        try { return ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
    }
    $content="redirected output`n";$digest=Digest ([Text.Encoding]::ASCII.GetBytes($content))
    $binary=New-Object byte[] 512;for ($i=0;$i -lt $binary.Length;$i++) { $binary[$i]=[byte]($i%256) }
    $full=New-Object byte[] 4096;for ($i=0;$i -lt $full.Length;$i++) { $full[$i]=[byte]($i%256) }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+133)*512
    $tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="stdio-$($case[0])-$($case[1])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $actions=@('storageinit','datainit','mmuinit','irqinit','timerstart',@{Text='appterm 30 /boot/IOCHK.ELF';Delay=4500},
            'appshell','runio - /data/stdout.txt /boot/ECHO.ELF "redirected output"',
            'runio /data/stdout.txt /data/stdcopy.txt /boot/CAT.ELF','sha256 /data/stdout.txt','sha256 /data/stdcopy.txt',
            'runio /data/stdout.txt - /boot/CAT.ELF','runio - /data/stdout.txt /boot/ECHO.ELF blocked','status',
            'runio - /boot/CONFIG.TXT /boot/ECHO.ELF blocked','runio /tmp/missing /tmp/shouldnot /boot/CAT.ELF',
            'runio - /tmp/invalid /boot/CONFIG.TXT','write /tmp/in abc','runio /tmp/in /tmp/raw /boot/IORAW.ELF bounds','sha256 /tmp/raw','rm /tmp/raw','rm /tmp/in',
            'runio - /tmp/overflow /boot/IORAW.ELF overflow','status','sha256 /tmp/overflow','rm /tmp/overflow',
            'runio - /tmp/fault /boot/IORAW.ELF fault','rm /tmp/fault','runio - /tmp/cancel /boot/SPIN.ELF',
            @{Text=[string][char]3;Raw=$true;Delay=600},'status','rm /tmp/cancel','run /boot/ECHO.ELF "terminal-restored"',
            'sync','exit','pwd','fileinfo','heaptest','pagetest','heapinfo','datainfo','timerstop','echo stdio-monitor-ok','halt')
        Session $disk "$name-create" $case[0] $case[1] $actions @(
            'PASS: stdio binary/EOF/bounds, no overwrite, leases, output limit, fault/timeout and stream cleanup.',
            ($digest+'  /data/stdout.txt'),($digest+'  /data/stdcopy.txt'),((Digest $binary)+'  /tmp/raw'),((Digest $full)+'  /tmp/overflow'),
            'child exit=74','child exit=125','child exit=130','shell: launch/wait failed','terminal-restored',
            'Open files: 0/16','pages=2 objects=3 capacity_bytes=544','generation=6','stdio-monitor-ok') 2
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'runio /data/stdout.txt - /boot/CAT.ELF','runio /data/stdcopy.txt - /boot/CAT.ELF','sha256 /data/stdout.txt','sha256 /data/stdcopy.txt',
            'exit','pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','echo stdio-reopened','halt') @(
            'redirected output',($digest+'  /data/stdout.txt'),($digest+'  /data/stdcopy.txt'),'generation=6','Open files: 0/16','stdio-reopened') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Unexpected stdio writes outside data banks' }
        $file=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
        try { $file.Position=260096L*512;if ($file.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Short data partition' } } finally { $file.Dispose() }
        $sha=[Security.Cryptography.SHA256]::Create()
        try {
            foreach ($bank in 0,1) {
                $at=512+$bank*66*512;$payload=$at+512
                if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne 'OUROSBK1' -or [BitConverter]::ToUInt32($bytes,$at+8) -ne 1 -or
                    [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt64($bytes,$at+16) -ne (5+$bank) -or
                    [BitConverter]::ToUInt32($bytes,$at+24) -ne 33280 -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$payload,33280)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Stdio snapshot metadata/hash failure' }
                foreach ($entry in 0,1) {
                    $record=$payload+$entry*64;$data=$payload+512+$entry*4096
                    $expectedName=if ($entry -eq 0) { 'stdout.txt' } else { 'stdcopy.txt' }
                    $expected=if ($entry -eq 1 -and !$bank) { '' } else { $content }
                    if ([Text.Encoding]::ASCII.GetString($bytes,$record,32).TrimEnd([char]0) -cne $expectedName -or
                        [BitConverter]::ToUInt32($bytes,$record+32) -ne $expected.Length -or
                        [Text.Encoding]::ASCII.GetString($bytes,$data,$expected.Length) -cne $expected) { throw 'Stdio persisted bytes/length/name differ' }
                }
                foreach ($entry in 2..7) { if ($bytes[$payload+$entry*64]) { throw 'Rejected redirection created an extra persistent file' } }
            }
        } finally { $sha.Dispose() }
        Write-Output "PASS: $name binary stdio, ownership, failure cleanup, cold-boot output and independent snapshot/untouched-sector checks."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
    Write-Output 'PASS: stdio used regular scratch disks; source image unchanged.'
    return
}
if ($Smoke) {
    $disk=Join-Path $ProjectRoot ('build/usb-tests/child-smoke-'+[guid]::NewGuid().ToString('N')+'.img')
    Copy-Item -LiteralPath $source -Destination $disk
    Session $disk 'child-smoke' 'cortex-a53' 'off' @('storageinit','mmuinit','irqinit','timerstart',
        @{Text='appterm 30 /boot/SPAWNCHK.ELF';Delay=4500},'apprun /boot/ORPHAN.ELF','appterm 10 /boot/TTYPAR.ELF','terminal-ok','appshell',
        'run /boot/HELLO.ELF','run /boot/SHELL.ELF','run /boot/HELLO.ELF','exit','run /boot/FAULT.ELF','run /boot/SPIN.ELF',@{Text=[string][char]3;Raw=$true;Delay=600},
        'echo resumed-parent','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
        'PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.',
        'children_started=11 children_reaped=11 last_child_code=124 cleanup=restored',
        'Parent exits with live child;','child exit=0','child exit=125','child exit=130','resumed-parent','Open files: 0/16',
        'PASS: child readline before parent wait blocks until terminal handoff.') 4
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
    return
}
$prefix=Range-Hash $source 0 (260097L*512)
$tailAt=(260096L+133)*512
$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
    $name="shell-$($case[0])-$($case[1])"
    $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
    Copy-Item -LiteralPath $source -Destination $disk
    $actions=@('storageinit','datainit','mmuinit','irqinit','timerstart',
        'apprun /boot/MUTATE.ELF /boot/TTYDENY.ELF','apprun /boot/COPYCHK.ELF',
        @{Text='appterm 30 /boot/SPAWNCHK.ELF';Delay=4500},'apprun /boot/ORPHAN.ELF','apprun /boot/CAPCHK.ELF /boot/CAPCHK.ELF /boot/CAPCHK.ELF',
        'appterm 10 /boot/TTYPAR.ELF','terminal-ok',
        'appshell','help','mkdir /tmp/shelltest','cd /tmp/shelltest','pwd',
        'write note "long original tail"','cat note','write note "x"','cat note','ls','rm note','cd /','rmdir /tmp/shelltest',
        'write /tmp/copy-seed "persistent shell data"','cp /tmp/copy-seed /data/shell.txt','rm /tmp/copy-seed','sha256 /data/shell.txt','sync','write /data/shell.txt "saved"','cat /data/shell.txt',
        'write /boot/CONFIG.TXT "blocked"','write /data/shouldnot "unterminated',('write /data/overflow "'+('x'*140)+'"'),
        ("echo editxy"+[char]8+[char]8+'-ok'),('echo bad'+[char]21+'echo cleared'),'cd /etc',
        'run ../boot/ECHO.ELF "quoted child" ""','run ../boot/PWD.ELF','pwd','run /boot/LEAK.ELF',
        'run /boot/FAULT.ELF','status','run /boot/CONFIG.TXT','status','run /boot/TTYCHK.ELF','terminal-ok',
        'run /boot/SPIN.ELF',@{Text=[string][char]3;Raw=$true;Delay=600},'status','echo child-cancel-survived',
        'run /boot/SHELL.ELF','run /boot/HELLO.ELF','exit',
        'cp /boot/HELLO.ELF /tmp/copied.elf','run /tmp/copied.elf','rm /tmp/copied.elf','exit','pwd',
        'appterm 10 /boot/TTYCHK.ELF','terminal-ok','appterm 10 /boot/TTYCHK.ELF',@{Text=[string][char]3;Raw=$true;Delay=600},
        'appterm 10 /boot/SPIN.ELF',@{Text=[string][char]3;Raw=$true;Delay=600},
        @{Text='appterm 1 /boot/TTYCHK.ELF';Delay=1800},'appterm 10 /boot/SHELL.ELF',@{Text=[string][char]4;Raw=$true;Delay=600},
        'appterm 10 /boot/SPIN.ELF',('x'*129),'appterm 301 /boot/SHELL.ELF','appterm 10 /boot/CONFIG.TXT',
        'appterm 1 /boot/SHELL.ELF',@{Text='run /boot/SPIN.ELF';Delay=1800},
        'fileinfo','heaptest','pagetest','heapinfo','datainfo','sync','timerinfo','timerstop','echo shell-monitor-ok','halt')
    Session $disk "$name-create" $case[0] $case[1] $actions @(
        'PASS: EL0 mutation bounds, atomic replacement, leases, read-only boot and cwd protection.',
        'PASS: noninteractive process cannot consume serial input.','OurOS EL0 shell.',
        'user:/tmp/shelltest$','long original tail','f 1 note','shell: operation failed',
        'shell: invalid quoting or too many words','shell: input rejected; command not executed','edit-ok','cleared',
        'PASS: terminal pointer/capacity checks and blocking line delivery.',
        'path=/boot/TTYCHK.ELF state=cancelled code=130','path=/boot/SPIN.ELF state=cancelled code=130',
        'path=/boot/TTYCHK.ELF state=timeout code=124','path=/boot/SPIN.ELF state=cancelled code=75',
        'Usage: appterm <1..300 seconds>','ELF rejected: unsupported ELF header','Open files: 0/16',
        'pages=2 objects=3 capacity_bytes=544','generation=3','shell-monitor-ok',($contentHash+'  /data/shell.txt'),
        'PASS: binary copy, zero/4096-byte bounds, no overwrite, SHA256 vectors, leases and pointer checks.',
        'PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.',
        'PASS: full process table refuses child creation.','Parent exits with live child;',
        'quoted child','children_started=11 children_reaped=11 last_child_code=124 cleanup=restored',
        'child exit=125','child exit=130','shell: launch/wait failed','child-cancel-survived',
        'path=/boot/SHELL.ELF state=timeout code=124','PASS: child readline before parent wait blocks until terminal handoff.') 15
    Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
        'appshell','cat /data/shell.txt','ls /data','exit','pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','echo shell-reopened','halt') @(
        'saved','f 5 shell.txt','generation=3','Open files: 0/16','shell-reopened') 1
    if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Unexpected disk writes outside data banks' }
    $f=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
    try { $f.Position=260096L*512;if ($f.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Short data partition' } } finally { $f.Dispose() }
    $sha=[Security.Cryptography.SHA256]::Create()
    try {
        foreach ($bank in 0,1) {
            $at=512+$bank*66*512
            if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne 'OUROSBK1' -or [BitConverter]::ToUInt32($bytes,$at+8) -ne 1 -or
                [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt32($bytes,$at+24) -ne 33280 -or
                [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                [Convert]::ToBase64String($sha.ComputeHash($bytes,$at+512,33280)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Snapshot hash/header failure' }
            $g=[BitConverter]::ToUInt64($bytes,$at+16);$content=if ($bank -eq 0) { 'saved' } else { 'persistent shell data' }
            $payload=$at+512
            if ($g -ne (3-$bank) -or [Text.Encoding]::ASCII.GetString($bytes,$payload,32).TrimEnd([char]0) -cne 'shell.txt' -or
                [BitConverter]::ToUInt32($bytes,$payload+32) -ne $content.Length -or
                [Text.Encoding]::ASCII.GetString($bytes,$payload+512,$content.Length) -cne $content) { throw 'Independent shell file bytes/generation differ' }
            foreach ($i in 1..7) { if ($bytes[$payload+$i*64]) { throw 'Rejected shell line created an unexpected file' } }
        }
    } finally { $sha.Dispose() }
    Write-Output "PASS: $name interactive editing, syscall bounds, mutations, Ctrl+C/EOF/overflow/deadline, cleanup and cold-boot data; independent snapshot and untouched boot checks."
}
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
Write-Output 'PASS: shell tests used regular scratch disks; source image unchanged.'
