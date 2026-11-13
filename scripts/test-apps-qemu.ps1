# Runs ELF applications only against regular project-local USB disk copies.
# It never writes the delivery image or opens a physical device.
[CmdletBinding()]
param([string]$Image='image_files/OurOS-Pi5-header-experimental-sd.img',[switch]$Tools)
. "$PSScriptRoot/windows-tools.ps1"
$source=(Resolve-Path -LiteralPath $Image).Path
$sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$out=Join-Path $ProjectRoot 'build/handoff-test'
$kernel=Join-Path $out 'kernel.bin'
$hello=Join-Path $ProjectRoot 'build/apps/HELLO.ELF'
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build handoff-test first' }
if (!(Test-Path -LiteralPath $hello)) { throw 'Build sample applications first' }
$helloBytes=[IO.File]::ReadAllBytes($hello)
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
function Run-Session([string]$Disk,[string]$Name,[string]$Cpu,[string]$Virt,[string[]]$Commands,[string[]]$Expected,[int]$Restorations) {
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $start.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device qemu-xhci,id=xhci,p3=0 -drive file=`"$Disk`",if=none,format=raw,readonly=off,id=appdisk -device usb-storage,bus=xhci.0,drive=appdisk -kernel `"$kernel`""
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $p=New-Object System.Diagnostics.Process;$p.StartInfo=$start;$started=$false
    try {
        if (!$p.Start()) { throw 'Cannot start QEMU' };$started=$true
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 1000
        foreach ($line in $Commands) {
            foreach ($c in ($line+"`r`n").ToCharArray()) { $p.StandardInput.Write($c);$p.StandardInput.Flush();Start-Sleep -Milliseconds 6 }
            $delay=if ($line -eq 'apptest' -or $line -eq 'tooltest') { 4500 } elseif ($line -eq 'taskiotest' -or $line.StartsWith('appinstall ') -or $line.StartsWith('apprun ') -or $line.StartsWith('appexec ')) { 1500 } else { 450 }
            Start-Sleep -Milliseconds $delay
        }
        Start-Sleep -Milliseconds 500
        if (!$p.HasExited) { $p.Kill() };$p.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$Name.log")
        $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
        foreach ($fragment in $Expected) { if (!$text.Contains($fragment)) { throw "$Name missing '$fragment'; see $out/$Name.log" } }
        if ([regex]::Matches($text,[regex]::Escape('App resources restored; monitor ready.')).Count -ne $Restorations) { throw "$Name unexpected app cleanup count; see log" }
        if ($text.Contains('FATAL') -or $text.Contains('FAIL:') -or $text.Contains('fault=yes') -or $text.Contains('Unknown command.')) { throw "$Name unexpected application/memory/IRQ failure; see log" }
        if ($Commands -contains 'apptest' -and ![regex]::IsMatch($text,'preemptions=[1-9][0-9]*')) { throw "$Name did not observe user timer preemption" }
        Write-Output "PASS: $Name"
    } finally { if ($started -and !$p.HasExited) { $p.Kill();$p.WaitForExit() };$p.Dispose() }
}
$appPass='PASS: EL0 applications, isolated memory, checked syscalls, preemption, contained faults/timeouts and resource cleanup.'
$taskPass='PASS: three tasks kept independent owned file cursors across yields/sleeps; foreign access rejected, six handles auto-closed and memory restored.'
$prefixHash=Segment-Hash $source 0 (260097L*512)
$tailOffset=(260096L+133)*512
$tailHash=Segment-Hash $source $tailOffset (134217728L-$tailOffset)
foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
    $stem="apps-$($case[0])-$($case[1])"
    $disk=Join-Path $ProjectRoot ('build/usb-tests/apps-'+$case[0]+'-'+$case[1]+'-'+[guid]::NewGuid().ToString('N')+'.img')
    Copy-Item -LiteralPath $source -Destination $disk
    $create=@('storageinit','datainit','apprun /boot/HELLO.ELF','mmuinit','irqinit','timerstart','apptest','appinfo','fileinfo','heaptest','pagetest','mmutest','taskiotest',
        'appinstall /boot/HELLO.ELF /data/hello.elf','appinstall /boot/HELLO.ELF /data/hello.elf',
        'appinstall /boot/CONFIG.TXT /data/bad','appinstall /boot/HELLO.ELF /tmp/refuse',
        'apprun /data/hello.elf','apprun /boot/COUNTER.ELF /boot/COUNTER.ELF /boot/COUNTER.ELF','apprun /boot/HELLO.ELF /boot/CONFIG.TXT',
        'appinfo','datainfo','fileinfo','heaptest','pagetest','heapinfo','timerinfo','timerstop','echo apps-monitor-ok','halt')
    $more=@();$restorations=7
    if ($Tools) {
        $extra=@('tooltest','appexec /boot/ECHO.ELF "hello from EL0" "Jack''s" ""',
            'cd /etc','appexec /boot/PWD.ELF','appexec /boot/CAT.ELF hostname','appexec /boot/LS.ELF .',
            'appexec /boot/LS.ELF hostname','pwd','cd /',
            'appexec /boot/LS.ELF /boot','appexec /boot/SLEEP.ELF 100','appexec /boot/CAT.ELF /missing',
            'appexec /boot/ECHO.ELF "unterminated','appexec /boot/ECHO.ELF a b c d e f g h',
            'fileinfo','heaptest','pagetest','heapinfo','appinfo')
        $at=[Array]::IndexOf($create,'timerstop')
        $create=@($create[0..($at-1)])+$extra+@($create[$at..($create.Length-1)])
        $restorations+=12
        $more=@('PASS: user arguments, private cwd, metadata, sleep/wake, idle resume and cleanup.',
            'PASS: EL0 argument bounds, private cwd and checked directory metadata.',
            'PASS: EL0 sleep retained stack, PID and file ownership.',
            'path=/boot/DOZER.ELF state=timeout code=124','sleeps=3 wakes=3',
            "hello from EL0 Jack's ",'f 6 hostname','f 528 HELLO.ELF','sleep complete',
            'cat: cannot open file','Usage: appexec <ELF> [arguments]')
    }
    Run-Session $disk "$stem-create" $case[0] $case[1] $create (@($appPass,$taskPass,
        'Hello from an OurOS EL0 application.','EL0 counter step=3 time_ms=',
        'PASS: EL0 owned file open/read/write/seek/close.',
        'PASS: EL0 syscall pointers rejected kernel/null/overflow ranges; valid cursor unchanged.',
        'PASS: EL0 busy loop completed under timer preemption.',
        'path=/boot/FAULT.ELF state=faulted code=125','path=/boot/SPIN.ELF state=timeout code=124',
        'path=/boot/KERN.ELF state=faulted code=125','path=/boot/TEXT.ELF state=faulted code=125',
        'path=/boot/XSTACK.ELF state=faulted code=125',
        'path=/boot/LEAK.ELF state=exited code=0','cleanup=restored',
        'Installed validated ELF: /data/hello.elf bytes=','synchronous commit and full readback verified.',
        'app: already exists','app: installation refused:','app: invalid path',
        'path=/data/hello.elf state=exited code=0','process=2 path=/boot/COUNTER.ELF state=exited code=0',
        'App run refused: require mmuinit, irqinit, timerstart and 1..3 valid paths.',
        'ELF rejected: unsupported ELF header','generation=2','Open files: 0/16','pages=2 objects=3 capacity_bytes=544','apps-monitor-ok')+$more) $restorations
    # Reopen the same disk file in a fresh emulator process, without copying
    # or reflashing. Executing the installed ELF tests persistent binary I/O.
    $reopen=@('storageinit','datainit','mmuinit','irqinit','timerstart','apprun /data/hello.elf',
        'appinfo','fileinfo','heaptest','pagetest','heapinfo','datainfo','timerinfo','timerstop','echo apps-reopened','halt')
    Run-Session $disk "$stem-reopen" $case[0] $case[1] $reopen @('Hello from an OurOS EL0 application.',
        'path=/data/hello.elf state=exited code=0','cleanup=restored','Open files: 0/16',
        'generation=2','pages=2 objects=3 capacity_bytes=544','apps-reopened') 1
    if ((Segment-Hash $disk 0 (260097L*512)) -ne $prefixHash -or (Segment-Hash $disk $tailOffset (134217728L-$tailOffset)) -ne $tailHash) {
        throw 'Guest changed MBR/boot files/data superblock or sectors outside data banks'
    }
    # Independent snapshot decoder: verify both banks' metadata and digests,
    # then compare every installed byte against the original built ELF.
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
                throw 'Independent snapshot metadata or SHA256 failed'
            }
            $g=[BitConverter]::ToUInt64($bytes,$at+16);if ($g -gt $generation) { $generation=$g;$chosen=$at+512 }
        }
        if ($generation -ne 2 -or [Text.Encoding]::ASCII.GetString($bytes,$chosen,32).TrimEnd([char]0) -cne 'hello.elf' -or
            [BitConverter]::ToUInt32($bytes,$chosen+32) -ne $helloBytes.Length) { throw 'Installed ELF namespace/length/generation incorrect' }
        for ($i=0;$i -lt $helloBytes.Length;$i++) { if ($bytes[$chosen+512+$i] -ne $helloBytes[$i]) { throw 'Installed ELF byte differs' } }
        foreach ($i in 1..7) { if ($bytes[$chosen+$i*64]) { throw 'Rejected install unexpectedly created a file' } }
    } finally { $hash.Dispose() }
    Write-Output "PASS: $stem EL0 syscalls/preemption/fault/timeout/owner cleanup, validated install and cold-boot execution; independent ELF bytes/snapshot hashes, unchanged boot/superblock/unused sectors."
}
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Source delivery image changed' }
Write-Output 'PASS: source image unchanged; all guest writes targeted regular build/usb-tests/apps-*.img copies.'
