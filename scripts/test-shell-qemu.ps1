# Interactive acceptance against regular scratch files only. No physical drive.
[CmdletBinding()]
param([string]$Image='image_files/OurOS-Pi5-header-experimental-sd.img',[switch]$Smoke,[switch]$Stdio,[switch]$Pipes,[switch]$Data2,[switch]$Data4,[switch]$Backup,[switch]$Rename,[switch]$Directories,[switch]$TreeRestore,[switch]$Replace,[switch]$Exec,[switch]$Environment,[switch]$Profiles,[switch]$UserMemory,[switch]$UserAllocator,[switch]$ProcessControl,[switch]$ShellJobs,
      [switch]$Data4Backup,[switch]$Startup,[switch]$DataRecovery,[switch]$LongShell,[switch]$MemoryPressure,[switch]$PeripheralIrq,[switch]$Reboot,[switch]$TwoJobs,[switch]$CommandLists,[switch]$Extents,[switch]$AppLifecycle,[switch]$NetworkWorker,[switch]$UdpSockets,[switch]$TcpSockets,[switch]$TrustTime,[switch]$PinnedCert,[switch]$TlsTranscript,[switch]$PackageDownload,[string]$ExpectedTreeBackup,
      [string]$RestoreFile='/boot/DEMO.BAK',
      [string]$LegacyImage='image_files/OurOS-Pi5-header-data-v1-before-capacity.img')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
$source=(Resolve-Path -LiteralPath $Image).Path
. "$PSScriptRoot/data-layout.ps1"
$layout=Get-OurOSDataLayout $source
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
function Session([string]$Disk,[string]$Name,[string]$Cpu,[string]$Virt,[object[]]$Actions,[string[]]$Expected,[int]$Restores,[string]$BootArgs,[bool]$FastClock,[bool]$ReadOnlyVirtio) {
    $si=New-Object Diagnostics.ProcessStartInfo
    $si.FileName=$QemuExe
    $si.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -kernel `"$kernel`""
    if ($ReadOnlyVirtio) {
        $si.Arguments+=" -global virtio-mmio.force-legacy=false -drive file=`"$Disk`",if=none,format=raw,readonly=on,id=shelldisk -device virtio-blk-device,drive=shelldisk"
    } else {
        $si.Arguments+=" -device qemu-xhci,id=xhci,p3=0 -drive file=`"$Disk`",if=none,format=raw,readonly=off,id=shelldisk -device usb-storage,bus=xhci.0,drive=shelldisk"
    }
    if ($BootArgs) { $si.Arguments+=" -append `"$BootArgs`"" }
    if ($FastClock) { $si.Arguments+=' -icount shift=10,sleep=off' }
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
if ($PackageDownload) {
    $hash='0'*64
    $request="appterm 10 /boot/PKGGET.ELF 192.0.2.2 4043 $hash DLHELLO.ELF"
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="package-download-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','datainit','mmuinit','irqinit','timerstart','appterm 5 /boot/PKGGET.ELF',
                $request,'utc set 2026-11-22T10:17:15Z',$request,
                @{Text='appterm 20 /boot/PKGGET.ELF check';Delay=2500},
                'fileinfo','pwd','timerstop','halt'
            ) @('Usage: PKGGET','Package certificate trust requires UTC.','TLS needs an active background Wi-Fi session.',
                'Package SHA-256 mismatch.','Package ELF rejected by the OurOS loader rules.',
                'PASS: package staging preserved the destination, verified readback, and released its files.',
                'path=/boot/PKGGET.ELF state=exited code=0 ','Open files: 0/16') 4
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($TlsTranscript) {
    $header=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'tests/tls13_demo.h')
    $pin=[regex]::Match($header,'TLS13_DEMO_PIN "([0-9a-f]{64})"').Groups[1].Value
    if (!$pin) { throw 'Missing TLS fixture pin' }
    $demo=@{Text='appterm 45 /boot/TLSCHK.ELF demo';Delay=7000}
    $pass='PASS: TLS 1.3 pinned leaf, CertificateVerify and server Finished.'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="tls-transcript-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','mmuinit','irqinit','timerstart','appterm 5 /boot/TLSECHO.ELF rng-check',"trustadd $pin",$demo,'trustclear',
                'utc set 2026-11-22T09:24:39Z',$demo,"trustadd $pin",$demo,
                'appterm 45 /boot/TLSCHK.ELF demo wrong.ouros.test',
                @{Text='appterm 45 /boot/TLSCHK.ELF transcript';Delay=7000},
                @{Text='appterm 45 /boot/TLSCHK.ELF finished';Delay=7000},
                'trustclear','fileinfo','pwd','timerstop','halt'
            ) @($pass,'TLS certificate trust requires UTC.','TLS certificate fingerprint is not enrolled.',
                'TLS certificate DNS name mismatch or unsupported name.','TLS CertificateVerify rejected.',
                'TLS server Finished rejected.','Open files: 0/16',
                'PASS: random syscall rejected bad arguments and preserved output with RNG unavailable.',
                'path=/boot/TLSECHO.ELF state=exited code=0 ') 7
            $log=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ([regex]::Matches($log,[regex]::Escape($pass)).Count -ne 1 -or
                [regex]::Matches($log,'path=/boot/TLSCHK\.ELF state=exited code=0 ').Count -ne 1 -or
                [regex]::Matches($log,'path=/boot/TLSCHK\.ELF state=exited code=1 ').Count -ne 5 -or
                (Get-FileHash -LiteralPath $disk).Hash -ne $original) {
                throw "$name unexpected app outcomes or disk mutation"
            }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($PinnedCert) {
    . "$PSScriptRoot/data-backup.ps1"
    . "$PSScriptRoot/data-format.ps1"
    $pin='7a2c46997c1c27ba1fb3fae02cc88759655c345eb3391e3220b1966a36cca467'
    $header=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'tests/cert_demo.h')
    [byte[]]$cert=[regex]::Matches($header,'0x([0-9a-f]{2})') | ForEach-Object { [Convert]::ToByte($_.Groups[1].Value,16) }
    [byte[]]$tampered=$cert.Clone()
    $tampered[$tampered.Length-1]=$tampered[$tampered.Length-1] -bxor 1
    $partition=New-OurOSDataPartition -Version 4
    $bank=New-OurOSDataBackup -Version 4 -Files @(@{Name='peer.der';Data=$cert},@{Name='tampered.der';Data=$tampered})
    $bank.CopyTo($partition,512)
    $check=@{Text='appterm 15 /boot/CERTCHK.ELF demo demo.ouros.test';Delay=5000}
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="pinned-cert-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            $file=[IO.File]::OpenWrite($disk)
            try { $file.Position=260096L*512;$file.Write($partition,0,$partition.Length) } finally { $file.Dispose() }
            $fixture=(Get-FileHash -LiteralPath $disk).Hash
            Session $disk $name $case[0] $case[1] @(
                'storageinit','datainit','mmuinit','irqinit','timerstart',"trustadd $pin",$check,'trustclear',
                'utc set 2026-11-22T09:07:40Z',$check,"trustadd $pin",$check,
                @{Text='appterm 15 /boot/CERTCHK.ELF /data/peer.der demo.ouros.test';Delay=5000},
                @{Text='appterm 15 /boot/CERTCHK.ELF /data/tampered.der demo.ouros.test';Delay=5000},
                'appterm 5 /boot/CERTCHK.ELF demo wrong.ouros.test',
                'utc set 2050-01-01T00:00:00Z',$check,'trustclear','utc clear','fileinfo','pwd','timerstop','halt'
            ) @("Certificate SHA-256 $pin",'Certificate trust check requires UTC.','Certificate fingerprint is not enrolled.',
                'PASS: pinned certificate name, self-signature, trust and UTC.','Certificate DNS name mismatch or unsupported name.',
                'Certificate is outside its UTC validity window.','Certificate signature rejected.','Open files: 0/16') 7
            $log=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ([regex]::Matches($log,'PASS: pinned certificate name, self-signature, trust and UTC\.').Count -ne 2 -or
                (Get-FileHash -LiteralPath $disk).Hash -ne $fixture) { throw "$name incorrect certificate result or disk mutation" }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($TrustTime) {
    $pin='a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3'
    $unknown='b'+$pin.Substring(1)
    $valid='2026-11-22T00:00:00Z 2026-11-24T00:00:00Z'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="trust-time-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','mmuinit','irqinit','timerstart','utc','trustlist','appterm 5 /boot/UTCCHECK.ELF',
                "trustadd $pin","trustcheck $pin $valid",'utc set 2100-02-29T00:00:00Z','utc set 2026-11-22T23:59:59Z',
                @{Text='appterm 5 /boot/UTCCHECK.ELF';Delay=2200},'utc','utc set 2026-11-22T00:00:00Z',
                "trustcheck $pin $valid","trustcheck $unknown $valid",
                "trustcheck $pin 2026-11-21T00:00:00Z 2026-11-22T00:00:00Z",
                "trustcheck $pin 2026-11-24T00:00:00Z 2026-11-25T00:00:00Z",
                'trustclear',"trustcheck $pin $valid",'utc clear','appterm 5 /boot/UTCCHECK.ELF','fileinfo','pwd','timerstop','halt'
            ) @('UTC unset. Use utc set','Trusted SHA-256 anchors=0','PASS: UTC unavailable and output preserved.',
                'Anchor enrolled for this boot.','Trust check refused. Set UTC first.','Usage: utc set',
                'PASS: UTC advanced with the counter.','UTC 2026-11-23T00:00:',
                'UTC would move backward.','Anchor and UTC validity passed.','Anchor is not enrolled.',
                'UTC falls outside the certificate validity window.','Trust anchors cleared.','UTC cleared.','Open files: 0/16') 3
            $log=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            foreach ($line in @('PASS: UTC unavailable and output preserved.','Anchor is not enrolled.','UTC falls outside the certificate validity window.')) {
                if ([regex]::Matches($log,[regex]::Escape($line)).Count -ne 2) { throw "$name expected exactly two instances of '$line'" }
            }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($TcpSockets) {
    $digest='a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="tcp-sockets-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','mmuinit','irqinit','timerstart',@{Text='tcpmodeltest';Delay=9500},
                'appterm 2 /boot/TCPCHK.ELF 192.0.2.2 34002','fileinfo','pwd','timerstop','halt'
            ) @('PASS: EL0 TCP connect, checked buffers, ordered stream, EOF and close.',
                'PASS: TCP worker and EL0 file transfer.',
                "PASS: TCP echoed 16384 bytes. SHA-256 $digest",
                'TCP requires an active background Wi-Fi session.','Open files: 0/16') 3
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($UdpSockets) {
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="udp-sockets-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','mmuinit','irqinit','timerstart',@{Text='udpmodeltest';Delay=5500},
                'appterm 2 /boot/UDPCHK.ELF 192.0.2.2 32001','fileinfo','pwd','timerstop','halt'
            ) @('PASS: EL0 UDP ownership, datagrams, checked buffers and receive timeout.',
                'PASS: UDP link loss woke the blocked receive and invalidated its handle.',
                'PASS: UDP worker and EL0 socket integration.',
                'UDP requires an active background Wi-Fi session.','Open files: 0/16') 3
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($NetworkWorker) {
    $pass='PASS: network worker yielded, resumed, preserved its stack and stopped.'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="network-worker-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'storageinit','mmuinit','irqinit','timerstart','networktest','networktest bogus',
                @{Text='networktest e10';Delay=2400},@{Text='schedtest';Delay=2500},
                'help','netup -R Example','netping','netrenew','netdown','appshell','networktest el0','netinfo',"netup -r Jack's",
                'echo shell-responsive','exit','fileinfo','pwd','timerstop','halt'
            ) @($pass,'EL0 timeout test: infinite user loop.',
                'Usage: networktest [el0] (e, lowercase L, digit zero)',
                'netup [-r|-R] <SSID> netping [IPv4] netrenew netinfo netdown',
                'PASS: three guarded kernel tasks yielded/slept/exited',
                'background session unavailable. No password requested.',
                'Type exit, then run this command at the > monitor prompt.',
                'No background Wi-Fi session.',"echo shell-responsive`r`nshell-responsive",'Open files: 0/16') 2
            $log=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ([regex]::Matches($log,[regex]::Escape($pass)).Count -ne 2) { throw "$name missed an EL0 or monitor worker pass" }
            if ($log -notmatch "netup -r Jack's\r?\nType exit, then run this command at the > monitor prompt") {
                throw "$name missed the prompt hint for an SSID containing an apostrophe"
            }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($AppLifecycle) {
    if ($layout.Version -notin 4,5) { throw 'AppLifecycle requires a v4 or v5 image' }
    $cpu,$virt=if ($layout.Version -eq 4) { 'cortex-a53','off' } else { 'cortex-a76','on' }
    $name="app-lifecycle-v$($layout.Version)"
    $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
    $prefix=Range-Hash $source 0 (260097L*512)
    Copy-Item -LiteralPath $source -Destination $disk
    $boot=@('storageinit','datainit','mmuinit','irqinit','timerstart')
    $end=@('cat /data/keep.txt','fileinfo','pwd','timerstop','halt')
    $keep="cat /data/keep.txt`r`nretained-user-file"
    try {
        Session $disk "$name-stage" $cpu $virt ($boot+@(
            'applist',@{Text='write /data/keep.txt retained-user-file';Delay=3200},
            @{Text='appadd demo v1 /boot/ECHO.ELF';Delay=8500},'applist','appstart demo "first revision"',
            @{Text='appupgrade demo v2 /boot/HELLO.ELF';Delay=7000},'appstart demo',
            'appupgrade demo v3 /boot/CONFIG.TXT','appupgrade demo v1 /boot/PWD.ELF',
            'fopen /data/.apps/demo.app r',@{Text='appupgrade demo v3 /boot/PWD.ELF';Delay=4400},
            'applist','stat /data/.apps/demo.3.elf','appstart demo')+$end) @(
            'No managed apps.','demo active=v1 previous=none','first revision','Hello from an OurOS EL0 application.',
            'app: installation refused:','app: already exists','Opened handle=',
            "app: root or current directory is busy`r`n> applist`r`ndemo active=v2 previous=v1",
            'path=/data/.apps/demo.3.elf',$keep,'Open files: 1/16') 3
        Session $disk "$name-rollback" $cpu $virt ($boot+@(
            'applist','appstart demo',@{Text='approllback demo';Delay=3400},
            'appstart demo "restored revision"')+$end) @(
            "applist`r`ndemo active=v2 previous=v1",'Hello from an OurOS EL0 application.',
            "approllback demo`r`ndemo active=v1 previous=v2",'restored revision',$keep,'Open files: 0/16') 2
        Session $disk "$name-uninstall" $cpu $virt ($boot+@(
            'applist','appstart demo "rollback after reboot"',@{Text='appuninstall demo';Delay=7500},
            'appstart demo','ls /data/.apps','applist')+$end) @(
            "applist`r`ndemo active=v1 previous=v2",'rollback after reboot','Uninstalled demo',
            "appstart demo`r`napp: path not found","ls /data/.apps`r`n> applist`r`nNo managed apps.",$keep,'Open files: 0/16') 1
        Session $disk "$name-reopen" $cpu $virt (@('storageinit','datainit','applist','appstart demo')+$end) @(
            'No managed apps.',"appstart demo`r`napp: path not found",$keep,'Open files: 0/16') 0
        if ((Range-Hash $disk 0 (260097L*512)) -cne $prefix) { throw "$name changed /boot or the data superblock" }
    } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($Extents) {
    if ($layout.Version -ne 5) { throw 'Extents requires a v5 image' }
    $bootLength=260097L*512
    $prefix=Range-Hash $source 0 $bootLength
    $patternHash='dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46'
    [byte[]]$fixture=New-Object byte[] 32768
    for ($i=0;$i -lt $fixture.Length;++$i) { $fixture[$i]=[byte](($i*13+5)%256) }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $hash=([BitConverter]::ToString($sha.ComputeHash($fixture))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="extents-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            $more=@(1..17 | ForEach-Object { @{Text="touch /data/slot-$_";Delay=180} })
            Session $disk "$name-create" $case[0] $case[1] (@(
                'df /data','exit',@{Text='datarestore /boot/DEMO.BAK';Delay=8500},'datarestore /boot/DEMO.BAK',
                'appshell','sha256 /data/pattern.bin',@{Text='cp /boot/EXTENT.BIN /data/big.bin';Delay=3500},
                'sha256 /data/big.bin','exit')+$more+@(
                'write /data/recovery older','write /data/recovery newer','datacheck','fileinfo','pwd','halt')) @(
                '/data extents v5 max_file_bytes=32768 nodes=0/32 persistent',
                'Data restore committed and read back','Data restore refused/failed: root or current directory is busy',
                "$patternHash  /data/pattern.bin","$hash  /data/big.bin",'matches_mounted=yes','Open files: 0/16') 2 'ouros.startup=shell'
            $normal=(Get-FileHash -LiteralPath $disk).Hash
            Session $disk "$name-reopen" $case[0] $case[1] @('df /data','sha256 /data/big.bin','cat /data/recovery','exit','datacheck','pwd','halt') @(
                '/data extents v5 max_file_bytes=32768 nodes=21/32 persistent',"$hash  /data/big.bin",
                "cat /data/recovery`r`nnewer",'matches_mounted=yes') 1 'ouros.startup=shell'
            if ((Get-FileHash -LiteralPath $disk).Hash -ne $normal) { throw "$name reboot changed the disk" }
            $created=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
            $newest=[regex]::Match($created,'Mounted /data bank=(\d+) generation=(\d+)')
            if (!$newest.Success) { throw 'Cannot locate the committed bank' }
            $file=[IO.File]::Open($disk,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
            try {
                $dataBase=260096L*512
                $index=$dataBase+(2+[int]$newest.Groups[1].Value*$layout.BankSectors)*512
                [byte[]]$table=New-Object byte[] $layout.PayloadBytes
                $file.Position=$index
                if ($file.Read($table,0,$table.Length) -ne $table.Length) { throw 'Short extent metadata' }
                $extent=0
                for ($slot=0;$slot -lt $layout.Files;++$slot) {
                    if ([Text.Encoding]::ASCII.GetString($table,64*$slot,32).TrimEnd([char]0) -ceq 'recovery') {
                        $extent=[BitConverter]::ToUInt32($table,$layout.DirectoryBytes+64*$slot);break
                    }
                }
                if ($extent -lt $layout.FirstExtent -or $extent -ge 2048) { throw 'Bad extent in fixture' }
                $file.Position=$dataBase+$extent*512;$first=$file.ReadByte()
                if ($first -ne [int][char]'n') { throw 'Wrong file selected for corruption' }
                $file.Position=$dataBase+$extent*512;$file.WriteByte([byte]($first -bxor 1))
            } finally { $file.Dispose() }
            $damaged=(Get-FileHash -LiteralPath $disk).Hash
            Session $disk "$name-fallback" $case[0] $case[1] @('sha256 /data/big.bin','cat /data/recovery','exit','datacheck','pwd','halt') @(
                "$hash  /data/big.bin","cat /data/recovery`r`nolder",'status=invalid payload','matches_mounted=yes') 1 'ouros.startup=shell'
            if ((Get-FileHash -LiteralPath $disk).Hash -ne $damaged -or (Range-Hash $disk 0 $bootLength) -cne $prefix) {
                throw "$name changed storage outside the intended commits or corruption"
            }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($TwoJobs) {
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="two-jobs-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @(
                'bg 30 /boot/SPIN.ELF','bg 1 /boot/SPIN.ELF','jobs','fg','stop','fg 99',
                'runio - /tmp/blocked echo capacity-failed','fg 4','echo timeout=$?','jobs',
                'bg 30 /boot/TTYCHK.ELF','fg 5','terminal-ok','jobs',
                'run /boot/ECHO.ELF foreground-peer','bgio 30 - /tmp/job-output echo redirected-peer',
                'stop 3','echo cancelled=$?','fg','cat /tmp/job-output',
                'write /tmp/lease-one one','write /tmp/lease-two two',
                'bg 30 /boot/PS.ELF worker /tmp/lease-one','bg 30 /boot/PS.ELF worker /tmp/lease-two',
                'jobs','exit','cat /tmp/blocked','write /tmp/lease-one reopened-one','write /tmp/lease-two reopened-two',
                'cat /tmp/lease-one','cat /tmp/lease-two',@{Text='appterm 60 /boot/SPAWNCHK.ELF';Delay=4500},
                'fileinfo','pwd','halt') @(
                'shell: specify a job PID','shell: unknown job PID','shell: use fg or stop first','job pid=3 pending',
                'timeout=124','cancelled=130','PASS: terminal pointer/capacity checks and blocking line delivery.',
                "`r`nforeground-peer`r`n","`r`nredirected-peer`r`n","`r`nreopened-one`r`n","`r`nreopened-two`r`n",
                'fs: path not found','PASS: spawn/wait args','Open files: 0/16','children_started=7 children_reaped=7') 2 'ouros.startup=shell'
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ([regex]::Matches($capture,'shell: specify a job PID').Count -ne 2 -or
                [regex]::Matches($capture,'(?m)^job pid=3 pending\r?$').Count -lt 2 -or
                $capture -match '(?m)^capacity-failed\r?$|heap_live_pages=[1-9]') { throw "$name job isolation or cleanup differs" }
            if ((Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw "$name changed the scratch disk" }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($CommandLists) {
    if ($layout.Version -ne 4) { throw 'CommandLists requires a v4 image with the current shell' }
    . "$PSScriptRoot/data-backup.ps1"
    . "$PSScriptRoot/data-format.ps1"
    $ascii=[Text.Encoding]::ASCII
    $nl=[Environment]::NewLine
    $workflow=@('  # comment','setenv KEY "left;right"','echo "script=$KEY"', 'cd /tmp; pwd',
        'run /boot/FAULT.ELF || echo caught-child=$?', 'cat /tmp/absent') -join $nl
    $bank=New-OurOSDataBackup -Version 4 -Files @(
        @{Name='workflow.txt';Data=$ascii.GetBytes($workflow)},
        @{Name='nested.txt';Data=$ascii.GetBytes('source /data/nested.txt')},
        @{Name='syntax.txt';Data=$ascii.GetBytes(@('echo before-bad','echo never-bad &&','echo never-after') -join $nl)},
        @{Name='oversize.txt';Data=$ascii.GetBytes(('x'*4097))},
        @{Name='many.txt';Data=$ascii.GetBytes((('echo never-many'+$nl)*33))},
        @{Name='long.txt';Data=$ascii.GetBytes(('x'*128))},
        @{Name='binary.txt';Data=[byte[]](65,0,66)})
    $partition=New-OurOSDataPartition -Version 4
    $bank.CopyTo($partition,512)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="command-lists-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            $file=[IO.File]::OpenWrite($disk)
            try { $file.Position=260096L*512;$file.Write($partition,0,$partition.Length) } finally { $file.Dispose() }
            $fixture=(Get-FileHash -LiteralPath $disk).Hash
            Session $disk $name $case[0] $case[1] @(
                'help','echo first; echo second && echo yes',
                'cat /tmp/absent && echo never-and || echo caught=$?',
                'echo pass || echo never-or && echo after-or',
                'echo "a; b && c || d"; echo e\;f',
                'setenv TOKEN "plain && echo never-expanded"; echo $TOKEN',
                'echo never-empty;; echo never-after',
                'echo never-unclosed; echo "unclosed',
                'echo never-max;a;b;c;d;e;f;g;h',
                'source /data/workflow.txt && echo never-source || echo script-status=$?',
                'getenv KEY; pwd; cd /',
                'source /data/nested.txt || echo nested-status=$?',
                'source /data/syntax.txt || echo syntax-status=$?',
                'source /data/oversize.txt; status','source /data/many.txt; status',
                'source /data/long.txt; status','source /data/binary.txt; status',
                'source /data/absent.txt || echo missing-status=$?',
                'exit; echo never-exited','fileinfo','pwd','halt') @(
                'help pwd cd ls','jobs | fg [PID] | stop [PID]','source PATH | lists with ; && ||',
                'caught=1','after-or','a; b && c || d','e;f','plain && echo never-expanded',
                'script=left;right','caught-child=125','script-status=1','nested-status=2','syntax-status=2',
                'before-bad','missing-status=1','Open files: 0/16','cleanup=restored') 1 'ouros.startup=shell'
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ($capture -match '(?m)^never-' -or $capture -notmatch '(?m)^first\r?\nsecond\r?\nyes\r?$' -or
                $capture -notmatch '(?m)^/tmp\r?$' -or
                [regex]::Matches($capture,'bounds \(command list\)').Count -ne 4 -or
                [regex]::Matches($capture,'shell: invalid script text or size limits\r?\n2').Count -ne 4) {
                throw "$name parsing, script bounds or status differs"
            }
            if ((Get-FileHash -LiteralPath $disk).Hash -ne $fixture) { throw "$name changed the scratch disk" }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($Reboot) {
    $banner='OurOS: AArch64 kernel booted'
    $fileHash='a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3  /data/reboot.bin'
    foreach ($case in @(@('cortex-a53','off','HVC'),@('cortex-a76','on','SMC'))) {
        $name="reboot-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            $reset="Reboot: storage synchronized and idle. Reset via PSCI $($case[2])."
            Session $disk "$name-monitor" $case[0] $case[1] @(
                @{Text='reboot';Delay=2000},'pwd','halt') @($reset,'Storage is opt-in') 0
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-monitor.log")
            if ([regex]::Matches($capture,[regex]::Escape($banner)).Count -ne 2 -or
                !$capture.Substring($capture.IndexOf($reset)).Contains($banner) -or
                (Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw "$name reboot without mounts differs" }
            Session $disk "$name-virtio" $case[0] $case[1] @(
                'storageinit','storageinfo',@{Text='reboot';Delay=2000},'storageinit','cat /boot/CONFIG.TXT','pwd','halt') @(
                $reset,'QEMU virtio-mmio block: ready (read-only driver)','kernel=OUROS.IMG') 0 '' $false $true
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-virtio.log")
            if ([regex]::Matches($capture,[regex]::Escape($banner)).Count -ne 2 -or
                [regex]::Matches($capture,'(?m)^> storageinit\r?\n/boot: FAT32 read-only').Count -ne 2 -or
                (Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw "$name reboot with read-only virtio differs" }
            $boot=Range-Hash $disk 0 (260096L*512)
            Session $disk $name $case[0] $case[1] @(
                @{Text='cp /boot/LARGE.BIN /data/reboot.bin';Delay=2500},'sha256 /data/reboot.bin','exit',
                'write /tmp/reboot-marker volatile','datacheck',@{Text='reboot';Delay=3500},
                'sha256 /data/reboot.bin','exit','cat /tmp/reboot-marker','datacheck','timertest','pwd','halt') @(
                $reset,$fileHash,'matches_mounted=yes','fs: path not found','PASS: periodic timer IRQs delivered') 2 'ouros.startup=shell'
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            $halves=$capture -split [regex]::Escape($reset)
            $generations=[regex]::Matches($capture,'Mounted /data bank=\d+ generation=(\d+)')
            if ($halves.Count -ne 2 -or !$halves[0].Contains($banner) -or !$halves[1].Contains($banner) -or
                !$halves[0].Contains($fileHash) -or !$halves[1].Contains($fileHash) -or
                $generations.Count -ne 2 -or $generations[0].Value -cne $generations[1].Value -or
                (Range-Hash $disk 0 (260096L*512)) -cne $boot) { throw "$name boot, persistence or boot partition differs" }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($PeripheralIrq) {
    $pass='PASS: peripheral IRQ acknowledged, deferred outside IRQ, timer still running.'
    $alarm='Peripheral IRQ source=PL031 alarm intid=34 received=1 deferred=1 timer_delta='
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="peripheral-irq-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        try {
            Session $disk $name $case[0] $case[1] @('exit','irqinfo',
                @{Text='irqtest';Delay=4500},'irqinfo',@{Text='irqtest';Delay=4500},'irqinfo',
                @{Text='timertest';Delay=800},'cat /boot/CONFIG.TXT','datacheck',
                'appshell',@{Text='run /boot/BUSY.ELF';Delay=2500},'run /boot/DF.ELF','exit','pwd','halt') @(
                $pass,$alarm,'peripheral_received=0 deferred=0 overflow=0 stuck=0',
                'peripheral_received=1 deferred=1 overflow=0 stuck=0',
                'peripheral_received=2 deferred=2 overflow=0 stuck=0',
                'Timer: running CPU_IRQ=unmasked INTID=30','PASS: periodic timer IRQs delivered',
                'kernel=OUROS.IMG','matches_mounted=yes','PASS: EL0 busy loop completed under timer preemption.',
                '/boot fat32','/data snapshot v4','child exit=0') 2 'ouros.startup=shell'
            $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
            if ([regex]::Matches($capture,[regex]::Escape($pass)).Count -ne 2 -or
                [regex]::Matches($capture,[regex]::Escape($alarm)+'[1-9]\d*').Count -ne 2 -or
                [regex]::Matches($capture,'(?m)^child exit=0\r?$').Count -ne 2 -or
                $capture -notmatch 'preemptions=[1-9]\d*' -or $capture -match '\bunexpected=[1-9]\d*') {
                throw "$name repeat delivery, preemption or controller counters differ"
            }
            if ((Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw "$name changed the scratch disk" }
        } finally { Remove-Item -LiteralPath $disk -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($MemoryPressure) {
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="memory-pressure-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk $name $case[0] $case[1] @('exit','meminfo','heapinfo','fileinfo','appshell',
            'run /boot/MEMDEMO.ELF pressure','run /boot/MEMCHK.ELF pressure','run /boot/MEMDEMO.ELF pressure',
            'exit','meminfo','heapinfo','fileinfo','pwd','halt') @(
            'Private heap free_pages=16 largest_run=16','quota_failures=0 fragmentation_failures=0 backing_failures=0 mapping_failures=0',
            'Full arena: quota_failures=1 free_pages=0','Fragmented arena: fragmentation_failures=1 largest_run=1',
            'PASS: memory pressure distinguished quota and fragmentation, retained live data and recovered all private pages.',
            'exhausted_allocations=0','page_limit_failures=0 backing_failures=0') 2 'ouros.startup=shell'
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
        foreach ($pattern in @('(?m)^managed_pages=\d+ free_pages=\d+ allocated_pages=\d+',
            '(?m)^pages=\d+ objects=\d+ capacity_bytes=\d+', '(?m)^Open files: 0/16')) {
            $snapshots=[regex]::Matches($capture,$pattern)
            if ($snapshots.Count -ne 2 -or $snapshots[0].Value -cne $snapshots[1].Value) { throw 'Resource baseline differs' }
        }
        if ([regex]::Matches($capture,'(?m)^child exit=0\r?$').Count -ne 3 -or
            [regex]::Matches($capture,'(?m)^quota_failures=0 fragmentation_failures=0 backing_failures=0 mapping_failures=0\r?$').Count -ne 2) {
            throw 'Memory reports or child exit status differ'
        }
        if ((Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw 'Memory test changed the scratch disk' }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($LongShell) {
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="long-shell-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $interrupt=@{Text=[string][char]3;Raw=$true;Delay=700}
        Session $disk "$name-interrupt" $case[0] $case[1] @(
            @{Text='echo discard-this';Raw=$true;Delay=300},$interrupt,
            'echo prompt-survived','bg 30 /boot/SPIN.ELF',$interrupt,'jobs','stop',
            'run /boot/SPIN.ELF',$interrupt,'echo child-status=$?','exit',
            'appshell','exit',@{Text='appterm 1 /boot/SHELL.ELF';Delay=1800},'pwd','halt') @(
            "user:/$ echo discard-this^C`r`nuser:/$ echo prompt-survived",
            'child-status=130','budget_seconds=unlimited','session_seconds=1',
            'path=/boot/SHELL.ELF state=timeout code=124') 3 'ouros.startup=shell'
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-interrupt.log")
        if ($capture -notmatch '(?m)^job pid=\d+ pending\r?$') { throw 'Prompt interrupt stopped the background job' }
        Session $disk "$name-idle" $case[0] $case[1] @(
            @{Text='uptime';Delay=200},@{Text='startup';Delay=800},@{Text='echo before-idle';Delay=4000},
            'echo shell-after-idle','exit','uptime','pwd','halt') @(
            'Starting serial shell from /boot/SHELL.ELF','budget_seconds=unlimited',
            "user:/$ echo before-idle`r`nbefore-idle","user:/$ echo shell-after-idle`r`nshell-after-idle") 1 '' $true
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-idle.log")
        $uptimes=[regex]::Matches($capture,'uptime_seconds=(\d+)')
        if ($uptimes.Count -ne 2 -or [long]$uptimes[1].Groups[1].Value-[long]$uptimes[0].Groups[1].Value -le 300) {
            throw 'Accelerated clock did not cross the former 300-second limit'
        }
        Session $disk "$name-exec" $case[0] $case[1] @(
            @{Text='exec /boot/SPIN.ELF';Delay=4000},'pwd','halt') @(
            'path=/boot/SPIN.ELF state=timeout code=124','execs=1') 1 'ouros.startup=shell' $true
        if ((Get-FileHash -LiteralPath $disk).Hash -ne $original) { throw 'Shell test changed the disk' }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    return
}
if ($DataRecovery) {
    if ($layout.Version -ne 4) { throw 'DataRecovery requires the v4 image' }
    . "$PSScriptRoot/data-format.ps1"
    . "$PSScriptRoot/data-backup.ps1"
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="recovery-$($case[0])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $stream=[IO.File]::OpenWrite($disk)
        try {
            [byte[]]$partition=New-OurOSDataPartition -Version 4
            $stream.Position=260096L*512
            $stream.Write($partition,0,$partition.Length)
        } finally { $stream.Dispose() }
        Session $disk "$name-create" $case[0] $case[1] @('datacheck','storageinit','datainit',
            @{Text='datacheck';Delay=1500},'dataexport previous',
            @{Text='write /data/recovery.txt older';Delay=2000},@{Text='write /data/recovery.txt newer';Delay=2000},'pwd','halt') @(
            'Data check unavailable:', 'bank=0 status=valid generation=1 matches_mounted=yes',
            'bank=1 status=empty','No older committed snapshot available.') 0
        $saved=(Get-FileHash -LiteralPath $disk).Hash
        Session $disk "$name-previous" $case[0] $case[1] @('storageinit','datainit',
            @{Text='datacheck';Delay=2000},@{Text='dataexport previous';Delay=8000},'cat /data/recovery.txt','pwd','halt') @(
            'bank=0 status=valid generation=3 matches_mounted=yes','bank=1 status=valid generation=2',
            '-----END OUROS DATA BACKUP V1-----',"cat /data/recovery.txt`r`nnewer") 0
        $exportedBackup=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-previous.log") -Output $exportedBackup
        $decoded=Get-OurOSDataBackup $exportedBackup
        if ($decoded.Version -ne 4 -or $decoded.Generation -ne 2 -or $decoded.Entries.Count -ne 1 -or
            $decoded.Entries[0].Path -cne 'recovery.txt' -or
            [Text.Encoding]::ASCII.GetString($decoded.Raw,$decoded.Entries[0].DataOffset,$decoded.Entries[0].Bytes) -cne 'older') {
            throw 'Previous serial archive differs'
        }
        if ((Get-FileHash -LiteralPath $disk).Hash -ne $saved) { throw 'Check/export changed disk bytes' }

        $stream=[IO.File]::OpenWrite($disk)
        try {
            $stream.Position=260100L*512
            $stream.WriteByte(0)
        } finally { $stream.Dispose() }
        $damaged=(Get-FileHash -LiteralPath $disk).Hash
        Session $disk "$name-fallback" $case[0] $case[1] @('storageinit','datainit',
            @{Text='datacheck';Delay=2000},@{Text='dataexport previous';Delay=2000},'cat /data/recovery.txt','pwd','halt') @(
            'bank=0 status=invalid payload generation=3','bank=1 status=valid generation=2 matches_mounted=yes',
            'Data export failed: corrupt filesystem',"cat /data/recovery.txt`r`nolder") 0
        if ((Get-FileHash -LiteralPath $disk).Hash -ne $damaged) { throw 'Fallback check changed disk bytes' }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: verified previous-generation serial backup, corrupt-newest fallback and read-only disk integrity.'
    return
}
if ($Startup) {
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="startup-$($case[0])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-manual" $case[0] $case[1] @('storageinit','datainit','mmuinit','startup',
            'setenv CHECK startup-retained','profile save','exit','pwd','halt') @('Storage is opt-in','Profile saved: /data/session.env') 1
        $saved=(Get-FileHash -LiteralPath $disk).Hash
        Session $disk "$name-auto" $case[0] $case[1] @('getenv CHECK','df /data','exit','startup','exit','pwd','fileinfo','halt') @(
            'Starting serial shell from /boot/SHELL.ELF','user:/$','Kernel monitor ready.','shell: profile loaded /data/session.env','startup-retained',
            "/data snapshot v$($layout.Version) max_file_bytes=$($layout.FileBytes)",'path=/boot/SHELL.ELF state=exited code=0','Open files: 0/16') 2 'console=ttyAMA0 ouros.startup=shell quiet'
        $log=Get-Content -Raw -LiteralPath (Join-Path $out "$name-auto.log")
        if ($log.IndexOf('user:/$') -gt $log.IndexOf('Kernel monitor ready.') -or
            [regex]::Matches($log,'Periodic physical timer started').Count -ne 1) { throw 'Automatic shell order or timer reuse differs' }
        if ((Get-FileHash -LiteralPath $disk).Hash -ne $saved) { throw 'Read-only shell session changed the disk' }
    }
    $policies=@{monitor='ouros.startup=monitor'; unknown='ouros.startup=shell-extra'; conflicting='ouros.startup=shell ouros.startup=monitor'}
    foreach ($policy in $policies.Keys) {
        Session $disk "startup-policy-$policy" 'cortex-a76' 'on' @('pwd','halt') @('Storage is opt-in','Kernel monitor ready.') 0 $policies[$policy]
    }
    # Damage only the disposable superblock magic. A failed mount must yield a usable monitor.
    $f=[IO.File]::OpenWrite($disk)
    try { $f.Position=260096L*512;$f.WriteByte(0) } finally { $f.Dispose() }
    $damaged=(Get-FileHash -LiteralPath $disk).Hash
    Session $disk 'startup-data-recovery' 'cortex-a76' 'on' @('pwd','halt') @('Startup stopped at /data.','No formatting attempted.','Kernel monitor ready.') 0 'ouros.startup=shell'
    if ((Get-FileHash -LiteralPath $disk).Hash -ne $damaged) { throw 'Recovery modified the invalid data volume' }
    $f=[IO.File]::OpenWrite($disk)
    try { $f.Position=510;$f.WriteByte(0) } finally { $f.Dispose() }
    $damaged=(Get-FileHash -LiteralPath $disk).Hash
    Session $disk 'startup-boot-recovery' 'cortex-a76' 'on' @('pwd','halt') @('Startup stopped at /boot.','Kernel monitor ready.') 0 'ouros.startup=shell'
    if ((Get-FileHash -LiteralPath $disk).Hash -ne $damaged -or (Get-FileHash -LiteralPath $source).Hash -ne $original) { throw 'Recovery modified a disk' }
    return
}
if ($Data4) {
    if ($layout.Version -ne 4) { throw 'Data4 acceptance requires the v4 delivery' }
    . "$PSScriptRoot/data-backup.ps1"
    . "$PSScriptRoot/data-format.ps1"
    [byte[]]$pattern=New-Object byte[] 16384;for ($i=0;$i -lt $pattern.Length;$i++) { $pattern[$i]=[byte](($i*29+7)%256) }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $patternHash=([BitConverter]::ToString($sha.ComputeHash($pattern))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $bigHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/BIGAPP.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    $bigBytes=(Get-Item -LiteralPath (Join-Path $ProjectRoot 'build/apps/BIGAPP.ELF')).Length
    if ($bigBytes -le 8192 -or $bigBytes -gt 16384) { throw 'BIGAPP does not cross the old file bound' }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="data4-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/V4CHK.ELF';Delay=10000},@{Text='appterm 300 /boot/DATACAP.ELF';Delay=45000},
            'apprun /boot/DF.ELF','mkdir /data/bin','appinstall /boot/BIGAPP.ELF /data/bin/big.elf',
            'apprun /data/bin/big.elf','appshell',@{Text='cp /boot/LARGE.BIN /data/large.bin';Delay=6000},'sha256 /data/large.bin',
            'sha256 /data/bin/big.elf','sync','exit','pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','halt') @(
            'OurOS snapshot v4; nodes=16 max_file_bytes=16384',
            'PASS: v4 16384-byte copy/hash, 8 KiB boundary write, EOF/overflow preservation, RAM limit, atomic replacement, mount queries/pointer checks and cleanup.',
            'PASS: v4 16384-byte binary file/copy/EOF, 16 slots, overflow preservation, RAM 4096-byte bound and cleanup.',
            '/data snapshot v4 max_file_bytes=16384 nodes=0/16 persistent','/boot fat32 v0 max_file_bytes=0 nodes=0/0 read-only',
            'Installed validated ELF: /data/bin/big.elf bytes=',
            'PASS: installed application reads all 10000 RX bytes beyond the former 8 KiB file limit.',
            ($patternHash+'  /data/large.bin'),($bigHash+'  /data/bin/big.elf'),'Open files: 0/16') 5
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'apprun /boot/DF.ELF','apprun /data/bin/big.elf','appshell','sha256 /data/large.bin','sha256 /data/bin/big.elf',
            'exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            '/data snapshot v4 max_file_bytes=16384 nodes=3/16 persistent',
            'PASS: installed application reads all 10000 RX bytes beyond the former 8 KiB file limit.',
            ($patternHash+'  /data/large.bin'),($bigHash+'  /data/bin/big.elf'),'path=/data/bin/big.elf state=exited code=0','Open files: 0/16') 3
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'v4 changed boot/superblock/unused sectors' }
        $f=[IO.File]::OpenRead($disk);[byte[]]$partition=New-Object byte[] 1048576
        try { $f.Position=260096L*512;if ($f.Read($partition,0,$partition.Length) -ne $partition.Length) { throw 'Short partition' } } finally { $f.Dispose() }
        $newest=if ([BitConverter]::ToUInt64($partition,528) -gt [BitConverter]::ToUInt64($partition,528+515*512)) { 0 } else { 1 }
        foreach ($bank in 0,1) {
            [byte[]]$archive=New-Object byte[] 263680;[Array]::Copy($partition,512+$bank*515*512,$archive,0,$archive.Length)
            if ([BitConverter]::ToUInt32($archive,12) -ne $bank) { throw 'Invalid bank identity' }
            $sha=[Security.Cryptography.SHA256]::Create()
            try {
                if ([Convert]::ToBase64String($sha.ComputeHash($archive,0,64)) -cne [Convert]::ToBase64String($archive[64..95])) { throw 'Invalid original bank header checksum' }
                [Array]::Clear($archive,12,4);$sha.ComputeHash($archive,0,64).CopyTo($archive,64)
            } finally { $sha.Dispose() }
            $archivePath=Join-Path $out "$name-bank$bank.bak";[IO.File]::WriteAllBytes($archivePath,$archive);$decoded=Get-OurOSDataBackup $archivePath
            if ($bank -eq $newest -and ($decoded.Version -ne 4 -or $decoded.Entries.Count -ne 3 -or
                @($decoded.Entries | Where-Object { $_.Path -ceq 'large.bin' -and $_.Bytes -eq 16384 -and $_.SHA256 -ceq $patternHash }).Count -ne 1 -or
                @($decoded.Entries | Where-Object { $_.Path -ceq 'bin/big.elf' -and $_.Bytes -eq $bigBytes -and $_.SHA256 -ceq $bigHash }).Count -ne 1)) { throw 'Independent v4 disk decode differs' }
        }
        Write-Output "PASS: $name v4 capacity/syscall bounds, larger installed ELF and exact 16 KiB data survive cold boot; independent bank/boot/tail checks."
    }
    foreach ($version in 1,2,3) {
        $name="data4-legacy-v$version";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $f=[IO.File]::Open($disk,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try { $f.Position=260096L*512;$bytes=New-OurOSDataPartition -Version $version;$f.Write($bytes,0,$bytes.Length) } finally { $f.Dispose() }
        $legacyLayout=Get-OurOSDataLayout $disk;$legacyPrefix=Range-Hash $disk 0 (260097L*512)
        Session $disk $name 'cortex-a76' 'on' @('storageinit','datainit','mmuinit','irqinit','timerstart','apprun /boot/DF.ELF',
            'apprun /boot/V4CHK.ELF','appinstall /boot/BIGAPP.ELF /data/too-big.elf',
            "write /data/legacy.txt retained-v$version",'cat /data/legacy.txt','pwd','fileinfo','timerstop','halt') @(
            "/data snapshot v$version max_file_bytes=$($legacyLayout.FileBytes) nodes=0/$($legacyLayout.Files) persistent",
            'V4CHK refused: needs v4 /data','path=/boot/V4CHK.ELF state=exited code=1',"retained-v$version",'Open files: 0/16') 2
        if ((Get-OurOSDataLayout $disk).Version -ne $version -or (Range-Hash $disk 0 (260097L*512)) -ne $legacyPrefix) { throw 'Legacy format was silently converted' }
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name.log")
        if ($capture.Contains('Installed validated ELF:')) { throw 'Oversize application installed on legacy volume' }
        Session $disk "$name-reopen" 'cortex-a76' 'on' @('storageinit','datainit','cat /data/legacy.txt','pwd','halt') @("retained-v$version") 0
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: v1/v2/v3 stay writable at old limits without conversion; v4 acceptance used scratch disks only.';return
}
if ($Data4Backup) {
    if ($layout.Version -ne 4) { throw 'Data4Backup requires a v4 image' }
    . "$PSScriptRoot/data-backup.ps1"
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    [byte[]]$pattern=New-Object byte[] 16384;for ($i=0;$i -lt $pattern.Length;$i++) { $pattern[$i]=[byte](($i*29+7)%256) }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $patternHash=([BitConverter]::ToString($sha.ComputeHash($pattern))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="backup4-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-export" $case[0] $case[1] @('storageinit','datainit',@{Text='datarestore /boot/DEMO.BAK';Delay=2000},
            'datainfo','cat /data/restore.txt','mmuinit','irqinit','timerstart','appshell',
            @{Text='cp /boot/LARGE.BIN /data/large.bin';Delay=2000},'sha256 /data/large.bin','exit',
            @{Text='dataexport';Delay=8000},'datarestore /boot/DEMO.BAK','cat /data/restore.txt','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'Data restore committed and read back; run datainfo and verify files.',
            'OurOS verified backup restore.',($patternHash+'  /data/large.bin'),
            '-----END OUROS DATA BACKUP V1-----','Data restore refused/failed: root or current directory is busy','Open files: 0/16') 1
        $exportedBackup=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-export.log") -Output $exportedBackup
        $decoded=Get-OurOSDataBackup $exportedBackup
        if ($decoded.Version -ne 4 -or $decoded.Entries.Count -ne 3 -or
            @($decoded.Entries | Where-Object { $_.Path -ceq 'large.bin' -and $_.Bytes -eq 16384 -and $_.SHA256 -ceq $patternHash }).Count -ne 1) { throw 'Actual v4 guest serial archive differs' }
        if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'build/storage-tests/test-backup.exe')) {
            & ./build/storage-tests/test-backup.exe --backup $exportedBackup
            if ($LASTEXITCODE -ne 0) { throw 'Native guest-format restore rejected actual serial archive' }
        }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','cat /data/restore.txt','mmuinit','irqinit','timerstart',
            'appshell','sha256 /data/large.bin','exit','pwd','fileinfo','timerstop','halt') @('OurOS verified backup restore.',($patternHash+'  /data/large.bin'),'Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'v4 restore/export changed boot or unused sectors' }
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: v4 FAT archive restore, actual 16 KiB guest serial export/host conversion, nonempty refusal and cold-boot data; scratch disks only.';return
}
if ($ShellJobs) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $envHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::ASCII.GetBytes("original`n")))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="jobs-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $busy=@('jobs','echo shell-still-live','mkdir /tmp/while-job','write /tmp/while-job/note background-ok','cat /tmp/while-job/note',
            'setenv MESSAGE parent-lives','getenv MESSAGE','cd /tmp/while-job','pwd','exec echo should-not-run',
            'pipe echo -- hash',
            'write /tmp/job-lease blocked','stop','echo stopped=$?','jobs','write /tmp/job-lease changed','cat /tmp/job-lease','cd /') |
            ForEach-Object { @{Text=$_;Delay=120} }
        $actions=@('storageinit','datainit','mmuinit','irqinit','timerstart','appshell','help','jobs','fg','status','stop','status',
            'bg 0 /boot/SPIN.ELF','bg 31 /boot/SPIN.ELF','bg -1 /boot/SPIN.ELF','bg 999999999999 /boot/SPIN.ELF',
            'bg nope /boot/SPIN.ELF','bg "" /boot/SPIN.ELF','status','bg 30 /boot/MISSING.ELF','jobs',
            'bg 30 /boot/CONFIG.TXT','jobs','bgio 30 /tmp/absent /tmp/no-out /boot/ECHO.ELF hi','ls /tmp/no-out',
            'write /tmp/job-lease lease',@{Text='bg 30 /boot/PS.ELF worker /tmp/job-lease';Delay=700})+$busy+@(
            'bg 30 /boot/PS.ELF done /tmp/job-lease','jobs','stop','echo completed=$?',
            'bg 30 /boot/PS.ELF fault /tmp/job-lease','jobs','fg','echo fault-code=$?',
            @{Text='bg 1 /boot/PS.ELF worker /tmp/job-lease';Delay=1600},'jobs','fg','echo timeout-code=$?',
            'bg 30 /boot/PS.ELF exec /tmp/job-lease','fg','echo exec-code=$?',
            'bg 30 /boot/PS.ELF input /tmp/job-lease','jobs','echo parent-kept-input','stop',
            'bg 30 /boot/TTYCHK.ELF','jobs','echo parent-before-fg','fg','terminal-ok','echo terminal-returned',
            'bg 30 /boot/SPIN.ELF','fg',@{Text=[string][char]3;Raw=$true;Delay=700},'echo fg-cancel=$?','jobs',
            'setenv MESSAGE original',@{Text='bgio 30 - /data/bg-env env MESSAGE';Delay=2000},'setenv MESSAGE later',
            @{Text='fg';Delay=1500},'cat /data/bg-env',
            'cd /home/user',@{Text='bgio 30 - /data/bg-cwd pwd';Delay=2000},'cd /tmp',
            @{Text='fg';Delay=1500},'cat /data/bg-cwd','cd /',
            @{Text='bgio 30 /data/bg-env /data/bg-hash hash';Delay=2000},@{Text='fg';Delay=1500},'cat /data/bg-hash',
            'bgio 30 - /data/bg-env echo should-not-overwrite','jobs','cat /data/bg-env',
            'pipe echo foreground-restored -- hash','ps','exit',
            'appterm 120 /boot/SHELL.ELF','bg 30 /boot/PS.ELF worker /tmp/job-lease',@{Text=[string][char]3;Raw=$true;Delay=700},
            'appterm 2 /boot/SHELL.ELF --noprofile',@{Text='bg 30 /boot/PS.ELF worker /tmp/job-lease';Delay=3500},
            'appshell','write /tmp/job-lease after-timeout','bg 30 /boot/PS.ELF worker /tmp/job-lease','exit',
            @{Text='mkdir /data/bin';Delay=1500},@{Text='appinstall /boot/SHELL.ELF /data/bin/shell.elf';Delay=1500},'appterm 120 /data/bin/shell.elf',
            'jobs',@{Text='bgio 30 - /data/installed-bg echo installed-bg';Delay=2000},@{Text='fg';Delay=1500},'cat /data/installed-bg',
            'bg 30 /boot/PS.ELF worker /tmp/job-lease','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt')
        Session $disk "$name-create" $case[0] $case[1] $actions @(
            'bg SECONDS PROGRAM [args]','No background job.','shell: timeout must be 1..30s',
            'shell: use fg or stop first','shell-still-live','background-ok','parent-lives','stopped=130','completed=37',
            'fault-code=125','timeout-code=124','exec-code=37','parent-kept-input','parent-before-fg','terminal-returned','fg-cancel=130',
            'PASS: terminal pointer/capacity checks and blocking line delivery.','PID PPID STATE PAGES PATH',
            'state=cancelled code=130','state=timeout code=124','path=/data/bin/shell.elf state=exited code=0',
            $envHash,'installed-bg','Open files: 0/16') 5
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($capture,'shell: timeout must be 1..30s').Count -ne 6 -or
            [regex]::Matches($capture,'shell: use fg or stop first').Count -ne 2) { throw 'Job bounds/busy refusal counts differ' }
        if ([regex]::IsMatch($capture,'(?m)^(should-not-run|should-not-overwrite)\r?$') -or
            ![regex]::IsMatch($capture,'(?m)^job pid=\d+ done code=37\r?$') -or
            ![regex]::IsMatch($capture,'(?m)^job pid=\d+ done code=124\r?$')) { throw 'Job outcome/launch gate differs' }
        if (![regex]::IsMatch($capture,'(?m)^/home/user\r?$') -or ![regex]::IsMatch($capture,'(?m)^original\r?$')) { throw 'Background cwd/environment snapshot differs' }
        foreach ($line in @([regex]::Matches($capture,'(?m)^process=[012] [^\r\n]*'))) {
            if (!$line.Value.Contains('heap_live_pages=0')) { throw 'Job teardown retained private pages' }
        }
        if ($capture.Contains('cleanup=not-run/failed')) { throw 'Job cleanup failed' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appterm 120 /data/bin/shell.elf','jobs','cat /data/bg-env','cat /data/bg-hash','cat /data/installed-bg',
            'bg 30 /boot/TTYCHK.ELF','fg','terminal-ok','bgio 30 - /data/cold-job echo cold-boot-job','fg','cat /data/cold-job',
            'exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'No background job.','original',$envHash,'installed-bg','cold-boot-job',
            'PASS: terminal pointer/capacity checks and blocking line delivery.','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Shell jobs changed boot/unused sectors' }
        Write-Output "PASS: $name bounded jobs, launch gates, retained outcomes, input/fg/cancel, inherited cwd/env, redirected storage and installed-shell cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: shell job tests used regular scratch disks; delivery unchanged.';return
}
if ($ProcessControl) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $checkPass='PASS: process snapshots, checked buffers, nonblocking poll, parent-only cancellation, stale PIDs, preserved exit/fault/exec/timeout results, leases and repeated cleanup.'
    $peerPass='PASS: three-slot parent/sibling snapshots and foreign PID refusal.'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="proc-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/PS.ELF check';Delay=7500},@{Text='apprun /boot/PS.ELF /boot/PS.ELF /boot/PS.ELF';Delay=2200},
            @{Text='appterm 60 /boot/PS.ELF terminal';Delay=1600},'parent-ok','child-ok','root-ok',
            'appexec /boot/PS.ELF orphan',
            'appterm 90 /boot/PS.ELF demo','status','stop','status','start','status','stop','start','exit',
            'appterm 90 /boot/PS.ELF demo',@{Text=[string][char]3;Raw=$true;Delay=900},
            @{Text='appterm 2 /boot/PS.ELF demo';Delay=3500},
            'appterm 60 /boot/TTYPAR.ELF','terminal-ok',@{Text='appterm 120 /boot/SPAWNCHK.ELF';Delay=6500},
            'appterm 120 /boot/HEAPCHK.ELF','appshell','ps','pipe ps -- hash',
            @{Text='runio - /data/procs.txt ps';Delay=15000},'cat /data/procs.txt','exit',
            'mkdir /data/bin','appinstall /boot/PS.ELF /data/bin/procs.elf','appexec /data/bin/procs.elf',
            'pwd','fileinfo','heaptest','pagetest','mmutest','timerstop','halt') @(
            $checkPass,$peerPass,'PID PPID STATE PAGES PATH',
            'PASS: cancelling a pending child reader preserves parent input and later terminal handoff.',
            'Parent exits with an outstanding owned child.','Child collected: exit=130','No outstanding child.','New child PID=',
            'state=cancelled code=130','state=timeout code=124',
            'PASS: child readline before parent wait blocks until terminal handoff.',
            'PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.',
            'PASS: user malloc/calloc/realloc/free alignment, shared small pages, zeroed growth, failure retention, invalid pointers, record/quota limits and complete cleanup.',
            'path=/data/bin/procs.elf state=exited code=0','Open files: 0/16') 12
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($capture,'Child collected: exit=130').Count -ne 2 -or
            [regex]::Matches($capture,'path=/boot/PS.ELF state=cancelled code=130').Count -ne 1 -or
            [regex]::Matches($capture,'path=/boot/PS.ELF state=timeout code=124').Count -ne 1) { throw 'Process demo cancellation/timeout outcomes differ' }
        # A timer can dispatch the child between spawn and the parent's wait.
        if (![regex]::IsMatch($capture,'(?im)^(\d+) 0 (?:ready|wait-child) 0 /boot/SHELL.ELF\r?\n\d+ \1 running 0 /boot/ps.elf')) { throw 'Shell/child snapshot identity or states differ' }
        $filePattern='(?m)^user:/\$ cat /data/procs\.txt\r?\n(PID PPID STATE PAGES PATH\r?\n(?:\d+ \d+ [a-z-]+ \d+ [^\r\n]+\r?\n)+)'
        $savedTable=[regex]::Match($capture,$filePattern)
        if (!$savedTable.Success) { throw 'Missing saved process snapshot readback' }
        foreach ($line in @([regex]::Matches($capture,'(?m)^process=[012] [^\r\n]*'))) {
            if (!$line.Value.Contains('heap_live_pages=0')) { throw 'Process-control teardown retained private pages' }
        }
        if ($capture.Contains('cleanup=not-run/failed')) { throw 'Process-control cleanup failed' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'cat /data/procs.txt','run procs','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'PID PPID STATE PAGES PATH','running 0 /data/bin/procs.elf','Open files: 0/16') 1
        $stored=Get-Content -Raw -LiteralPath (Join-Path $out "$name-reopen.log")
        $reopenedTable=[regex]::Match($stored,$filePattern)
        if (!$reopenedTable.Success -or $savedTable.Groups[1].Value.Replace("`r`n","`n") -cne
            $reopenedTable.Groups[1].Value.Replace("`r`n","`n")) { throw 'Persistent snapshot contents differ after cold boot' }
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Process-control tests changed boot/unused sectors' }
        Write-Output "PASS: $name snapshots/ownership, poll/cancel/reap, three-slot peers, terminal handoff, orphan/cancel/timeout cleanup and installed-viewer cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: process-control tests used regular scratch disks; delivery unchanged.';return
}
if ($UserAllocator) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $basicPass='PASS: user malloc/calloc/realloc/free alignment, shared small pages, zeroed growth, failure retention, invalid pointers, record/quota limits and complete cleanup.'
    $parentPass='PASS: byte-allocator leaks reclaimed on exit/exec/fault/timeout; failed exec preserves context/data and child slots remain reusable.'
    $demoText="Small buffers: requested=768 bytes; reserved_pages=1`nAfter resize: requested=5744 bytes; reserved_pages=3`nAfter calloc: requested=6000 bytes; reserved_pages=4`nReleased buffers; private_pages=0`n"
    $demoSha=[Security.Cryptography.SHA256]::Create()
    try { $demoHash=([BitConverter]::ToString($demoSha.ComputeHash([Text.Encoding]::ASCII.GetBytes($demoText)))).Replace('-','').ToLowerInvariant() } finally { $demoSha.Dispose() }
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="ualloc-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/HEAPCHK.ELF resize';Delay=2500},
            @{Text='appterm 120 /boot/HEAPCHK.ELF';Delay=4500},@{Text='apprun /boot/HEAPCHK.ELF /boot/HEAPCHK.ELF /boot/HEAPCHK.ELF';Delay=4500},
            @{Text='appterm 120 /boot/HEAPCHK.ELF parent';Delay=12000},'appexec /boot/HEAPCHK.ELF leak',
            'appexec /boot/HEAPCHK.ELF exec','appexec /boot/HEAPCHK.ELF execbad','appexec /boot/HEAPCHK.ELF uaf',
            'appexec /boot/HEAPCHK.ELF nx',@{Text='appexec /boot/HEAPCHK.ELF timeout';Delay=3500},
            @{Text='appterm 120 /boot/HEAPCHK.ELF cancel';Delay=1000},@{Text=[string][char]3;Raw=$true;Delay=800},
            'appshell','run heapdemo','run heapchk','pipe heapdemo -- hash',
            @{Text='runio - /data/byteheap.txt heapdemo';Delay=20000},'sha256 /data/byteheap.txt','cat /data/byteheap.txt','exit',
            @{Text='mkdir /data/bin';Delay=1500},@{Text='appinstall /boot/HEAPDEMO.ELF /data/bin/byteheap.elf';Delay=1500},
            'appexec /data/bin/byteheap.elf',
            'pwd','fileinfo','heaptest','pagetest','mmutest','timerstop','halt') @(
            $basicPass,$parentPass,'HEAPCHK ready for cancellation.','state=cancelled code=130','state=timeout code=124',
            'state=exited code=23','state=exited code=37',
            'Small buffers: requested=768 bytes; reserved_pages=1','After resize: requested=5744 bytes; reserved_pages=3',
            'After calloc: requested=6000 bytes; reserved_pages=4','Released buffers; private_pages=0',
            'PASS: in-place page resize and realloc, preserved contents, zeroed growth and reclaimed tail pages.',
            $demoHash,($demoHash+'  /data/byteheap.txt'),'path=/data/bin/byteheap.elf state=exited code=0','Open files: 0/16') 13
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($capture,'process=[012] path=/boot/HEAPCHK.ELF state=exited code=0[^\r\n]*heap_peak_pages=16').Count -lt 4 -or
            [regex]::Matches($capture,[regex]::Escape($basicPass)).Count -ne 5) { throw 'Byte allocator default/concurrent results differ' }
        $faults=@([regex]::Matches($capture,'path=/boot/HEAPCHK.ELF state=faulted code=125[^\r\n]*'))
        if ($faults.Count -ne 2) { throw 'Missing contained allocator use-after-free/NX fault' }
        $faultEc=@($faults | ForEach-Object { $m=[regex]::Match($_.Value,'ESR=0x([0-9a-f]+)');if (!$m.Success) { throw 'Missing allocator fault diagnostic' };[Convert]::ToUInt64($m.Groups[1].Value,16) -shr 26 })
        if (($faultEc -join ',') -cne '36,32') { throw 'Allocator faults have unexpected exception classes' }
        foreach ($line in @([regex]::Matches($capture,'(?m)^process=[012] [^\r\n]*'))) {
            if (!$line.Value.Contains('heap_live_pages=0')) { throw 'Application left private pages after teardown' }
        }
        if ($capture.Contains('cleanup=not-run/failed')) { throw 'Byte allocator cleanup failed' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'sha256 /data/byteheap.txt','run byteheap','run heapchk','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            ($demoHash+'  /data/byteheap.txt'),'After calloc: requested=6000 bytes; reserved_pages=4',
            $basicPass,'Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Byte allocator tests changed boot/unused sectors' }
        Write-Output "PASS: $name packed allocations/resize, three-process patterns, exit/exec/fault/timeout/cancel cleanup, streams and installed-app cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: byte allocator tests used regular scratch disks; delivery unchanged.';return
}
if ($UserMemory) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $demoText="Application page arena: 64 KiB; live pages=0`nWorking buffer: 16 KiB; live pages=4; checksum=2088960`nReleased buffer; live pages=0`n"
    $demoSha=[Security.Cryptography.SHA256]::Create()
    try { $demoHash=([BitConverter]::ToString($demoSha.ComputeHash([Text.Encoding]::ASCII.GetBytes($demoText)))).Replace('-','').ToLowerInvariant() } finally { $demoSha.Dispose() }
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="usermem-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/MEMCHK.ELF';Delay=4500},@{Text='apprun /boot/MEMCHK.ELF /boot/MEMCHK.ELF /boot/MEMCHK.ELF';Delay=4500},
            @{Text='appterm 120 /boot/MEMCHK.ELF parent';Delay=12000},'appexec /boot/MEMCHK.ELF leak',
            'appexec /boot/MEMCHK.ELF exec','appexec /boot/MEMCHK.ELF execbad','appexec /boot/MEMCHK.ELF uaf',
            'appexec /boot/MEMCHK.ELF nx','appexec /boot/MEMCHK.ELF guard','appexec /boot/MEMCHK.ELF upper',
            @{Text='appexec /boot/MEMCHK.ELF timeout';Delay=3500},@{Text='appterm 120 /boot/MEMCHK.ELF cancel';Delay=1000},
            @{Text=[string][char]3;Raw=$true;Delay=800},'appshell','run memdemo','run memchk','pipe memdemo -- hash',
            @{Text='runio - /data/memory.txt memdemo';Delay=7000},'sha256 /data/memory.txt','cat /data/memory.txt','exit',
            'mkdir /data/bin','appinstall /boot/MEMDEMO.ELF /data/bin/memory.elf','appexec /data/bin/memory.elf',
            'pwd','fileinfo','heaptest','pagetest','mmutest','timerstop','halt') @(
            'PASS: private dynamic pages zeroed, quota/fragmentation/reuse, cross-page syscall buffers, freed-pointer rejection, concurrent isolation and complete release.',
            'PASS: leaked pages reclaimed on child exit/exec/fault/timeout; exec identity/cursor retained, failed exec preserves pages and repeated slots stay clean.',
            'MEMCHK ready for cancellation.','state=cancelled code=130','state=timeout code=124',
            'state=exited code=23','state=exited code=37',
            'Application page arena: 64 KiB; live pages=0','Working buffer: 16 KiB; live pages=4; checksum=2088960',
            'Released buffer; live pages=0',$demoHash,($demoHash+'  /data/memory.txt'),
            'path=/data/bin/memory.elf state=exited code=0','Open files: 0/16') 14
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($capture,'process=[012] path=/boot/MEMCHK.ELF state=exited code=0[^\r\n]*heap_peak_pages=16[^\r\n]*heap_allocations=20[^\r\n]*heap_free_calls=20').Count -lt 4) { throw 'Private page tests/counters differ' }
        $faults=@([regex]::Matches($capture,'path=/boot/MEMCHK.ELF state=faulted code=125[^\r\n]*'))
        if ($faults.Count -ne 4) { throw 'Missing contained use-after-free/NX/lower/upper guard fault' }
        foreach ($fault in $faults) {
            $m=[regex]::Match($fault.Value,'ESR=0x([0-9a-f]+) FAR=0x([0-9a-f]+)')
            if (!$m.Success -or !$fault.Value.Contains('heap_live_pages=0') -or !$fault.Value.Contains('heap_peak_pages=16')) { throw 'Faulted heap resources/diagnostic differs' }
        }
        $faultEc=@($faults | ForEach-Object { $m=[regex]::Match($_.Value,'ESR=0x([0-9a-f]+)');[Convert]::ToUInt64($m.Groups[1].Value,16) -shr 26 })
        if (($faultEc -join ',') -cne '36,32,36,36') { throw 'Unexpected data/instruction exception classes' }
        if ($capture.Contains('cleanup=not-run/failed')) { throw 'Page cleanup failed' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'sha256 /data/memory.txt','run memory','run memchk','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            ($demoHash+'  /data/memory.txt'),'Working buffer: 16 KiB; live pages=4; checksum=2088960',
            'PASS: private dynamic pages zeroed, quota/fragmentation/reuse, cross-page syscall buffers, freed-pointer rejection, concurrent isolation and complete release.',
            'Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Application memory tests changed boot/unused sectors' }
        Write-Output "PASS: $name private pages, zero/reuse, three-process isolation, faults/guards/NX, exit/exec/timeout/cancel cleanup, stream/storage use and installed-app cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: private memory tests used regular scratch disks; delivery unchanged.';return
}
if ($Profiles) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $profilePass='PASS: profile save/load/full replacement, literal/empty/max values, leases, read-only/cross-mount refusal, corrupt-input rollback, CRLF and checked syscall pointers.'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="profile-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/PROFCHK.ELF';Delay=5000},@{Text='appterm 120 /boot/ENVCHK.ELF';Delay=5000},
            'appshell','setenv MESSAGE "hello profile"','echo "$MESSAGE"','echo ''${MESSAGE}''','echo \$MESSAGE','echo "$MISSING" x',
            'write /tmp/${MISSING}exp.txt "$MESSAGE"','cat /tmp/exp.txt','setenv LITERAL ''$MESSAGE''','echo "$LITERAL"',
            'run echo "$MESSAGE" ""','setenv CMD echo','run $CMD arg-expanded','setenv CODE /boot/ECHO.ELF',
            'missing-program','echo "result=$?"','cat /tmp/absent','echo "result=$?"','echo ${BROKEN','echo "result=$?"',
            'echo "unterminated','echo "result=$?"',@{Text=('setenv LONG '+('L'*110));Delay=550},
            'echo $LONG$LONG$LONG$LONG$LONG','echo "result=$?"','unsetenv LONG','echo guard-after-overflow',
            'profile save','cat /data/session.env','setenv MESSAGE transient','profile load','getenv MESSAGE','echo "$LITERAL"','exit',
            'mkdir /data/bin','appinstall /boot/SHELL.ELF /data/bin/shell.elf','appterm 120 /data/bin/shell.elf',
            'echo "$MESSAGE"','setenv HOME /etc','cd','pwd','profile save','exit',
            'appshell','env','echo "$MESSAGE"','cd','pwd','profile save /data/other.env','setenv MESSAGE changed',
            'profile load /data/other.env','profile load /boot/CONFIG.TXT','echo "retained=$MESSAGE"',
            'write /data/session.env broken-profile','exit',
            'appshell','getenv MESSAGE','echo "empty=${MESSAGE}"','profile load /data/other.env','profile save','exit',
            'appterm 120 /boot/SHELL.ELF --noprofile','echo "noprofile=${MESSAGE}"','profile load /data/session.env',
            'echo "manual=$MESSAGE"','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            $profilePass,'path=/boot/PROFCHK.ELF state=exited code=0',
            'PASS: checked environment names/values/buffers, full-table recovery, private spawn snapshots, child exec preservation, failed-exec retention and cleanup.',
            'arg-expanded','result=127','result=1','result=2','guard-after-overflow',
            'shell: invalid quoting, expansion or argument bounds','Profile saved: /data/session.env','OUROSENV1',
            'LITERAL=$MESSAGE','Profile loaded: /data/session.env','shell: profile loaded /data/session.env',
            'Installed validated ELF: /data/bin/shell.elf bytes=', 'path=/data/bin/shell.elf state=exited code=0',
            'HOME=/etc','Profile loaded: /data/other.env','shell: profile refused; environment retained',
            'retained=hello profile','empty=','noprofile=','manual=hello profile','Open files: 0/16') 7
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        foreach ($literal in @('${MESSAGE}','$MESSAGE','hello profile ',' x','empty=','noprofile=')) {
            if (![regex]::IsMatch($capture,'(?m)^'+[regex]::Escape($literal)+'\r?$')) { throw "Missing literal/empty/single-word expansion '$literal'" }
        }
        if ([regex]::Matches($capture,'(?m)^result=2\r?$').Count -ne 3 -or
            [regex]::Matches($capture,'(?m)^manual=hello profile\r?$').Count -ne 1) { throw 'Expansion rejection/status or noprofile recovery differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appterm 60 /data/bin/shell.elf','env','echo "cold=$MESSAGE"','echo "$LITERAL"','cd','pwd',
            'run env MESSAGE','exec env MESSAGE','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'shell: profile loaded /data/session.env','cold=hello profile','HOME=/etc','user:/etc$',
            'hello profile','path=/boot/env.elf state=exited code=0','execs=1','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Profile work changed boot/unused sectors' }
        Write-Output "PASS: $name profile ABI/rollback, expansion/status bounds, automatic/recovery loads, installed shell and profile cold boot."
    }
    . "$PSScriptRoot/data-format.ps1"
    foreach ($version in 1,2) {
        $compat=Join-Path $ProjectRoot ('build/usb-tests/profile-compat-v'+$version+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $compat
        $f=[IO.File]::Open($compat,[IO.FileMode]::Open,[IO.FileAccess]::Write)
        try { $f.Position=260096L*512;$bytes=New-OurOSDataPartition -Version $version;$f.Write($bytes,0,$bytes.Length) } finally { $f.Dispose() }
        Session $compat "profile-compat-v$version-create" 'cortex-a76' 'on' @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/PROFCHK.ELF';Delay=5000},'appshell',('setenv MESSAGE legacy-v'+$version),'profile save','exit',
            'pwd','fileinfo','timerstop','halt') @(("OurOS snapshot v$version; files="),$profilePass,'Profile saved: /data/session.env','Open files: 0/16') 2
        Session $compat "profile-compat-v$version-reopen" 'cortex-a76' 'on' @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'echo "$MESSAGE"','exec env MESSAGE','pwd','fileinfo','timerstop','halt') @(
            ("OurOS snapshot v$version; files="),'shell: profile loaded /data/session.env',('legacy-v'+$version),'Open files: 0/16') 1
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: profiles use regular scratch disks; delivery unchanged.';return
}
if ($Environment) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="env-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 120 /boot/ENVCHK.ELF';Delay=5000},
            'apprun /boot/ENVRACE.ELF /boot/ENVRACE.ELF /boot/ENVRACE.ELF',
            'appshell','help','env','path','getenv HOME','cd','pwd','cd /','setenv MESSAGE "hello from environment"','getenv MESSAGE',
            'run env MESSAGE','pipe env -- /boot/CAT.ELF','runio - /tmp/env-vars env','cat /tmp/env-vars',
            'setenv PATH /boot','hello','mkdir /data/bin','cp /boot/ECHO.ELF /data/bin/speak',
            'speak should-not-run','status','setenv PATH /data/bin','speak data-only-path','run env MESSAGE',
            '/boot/ENV.ELF MESSAGE','setenv PATH /data/bin:/boot','speak search-order',
            'cp /boot/ECHO.ELF /data/bin/hello.elf','hello local-before-boot','setenv PATH /boot:/data/bin','hello',
            'setenv PATH :.:relative::/data/bin:','speak skipped-relative-components','setenv PATH :.:relative::','speak should-not-run','status',
            'setenv PATH ""','path','hello should-not-run','/boot/HELLO.ELF',
            'unsetenv PATH','hello should-not-run','/boot/ENV.ELF MESSAGE','setenv PATH /data/bin:/boot',
            'setenv HOME /etc','cd','pwd','cd /','unsetenv MESSAGE','getenv MESSAGE','setenv MESSAGE exec-kept',
            'exec env MESSAGE','appshell','getenv MESSAGE','status','path','env',
            'setenv SESSION fresh-session','setenv 1BAD bad-name','getenv SESSION','unsetenv ABSENT','exit',
            @{Text='appterm 120 /boot/ENVCHK.ELF';Delay=5000},'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'PASS: checked environment names/values/buffers, full-table recovery, private spawn snapshots, child exec preservation, failed-exec retention and cleanup.',
            'PASS: both pipeline environments inherited privately and survived exec with binary streams.',
            'PASS: concurrent root environments remain private across yields and sleeps.',
            'env | getenv NAME | setenv NAME VALUE | unsetenv NAME',
            'PATH=/data/bin:/boot','HOME=/home/user','TERM=serial','user:/home/user$',
            'hello from environment','MESSAGE=hello from environment','data-only-path','search-order','local-before-boot',
            'skipped-relative-components','user:/etc$','exec-kept','fresh-session',
            'path=/boot/env.elf state=exited code=0','execs=1','Open files: 0/16') 5
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($capture,'path=/boot/ENVRACE.ELF state=exited code=0').Count -ne 3) { throw 'Concurrent private environment checks differ' }
        if ([regex]::Matches($capture,'Hello from an OurOS EL0 application\.').Count -ne 3) { throw 'Configured PATH precedence/empty/unset behavior differs' }
        if ([regex]::IsMatch($capture,'(?m)^should-not-run\r?$') -or
            [regex]::Matches($capture,'\$ status\r?\n127\r?\n').Count -ne 2) { throw 'Disabled PATH lookup or missing-program status differs' }
        $fresh=$capture.Substring($capture.LastIndexOf('OurOS EL0 shell. Type help; exit returns to monitor.'))
        if ($fresh.Contains('MESSAGE=') -or $fresh.Contains('HOME=/etc') -or ![regex]::IsMatch($fresh,'getenv MESSAGE\r?\nshell: operation failed\r?\n')) { throw 'Environment leaked into a new monitor session' }
        if (![regex]::IsMatch($capture,'getenv SESSION\r?\nfresh-session\r?\n') -or ![regex]::IsMatch($fresh,'\$ status\r?\n1\r?\n')) { throw 'Invalid name or missing-key handling differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'env','getenv SESSION','speak persisted-app-default-path','setenv MESSAGE after-reboot','exec env MESSAGE',
            'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'PATH=/data/bin:/boot','HOME=/home/user','TERM=serial','shell: operation failed',
            'persisted-app-default-path','after-reboot','path=/boot/env.elf state=exited code=0','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Environment phase wrote outside data banks' }
        Write-Output "PASS: $name checked environment ABI, spawn/exec/pipeline copies, configurable PATH/HOME, fresh sessions and installed-app cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: environment acceptance uses regular scratch disks; delivery unchanged.';return
}
if ($Exec) {
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="exec-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 60 /boot/EXECCHK.ELF';Delay=3500},@{Text='appterm 60 /boot/EXECCHK.ELF parent';Delay=5000},
            @{Text='apprun /boot/EXECCHK.ELF /boot/EXECCHK.ELF /boot/EXECCHK.ELF';Delay=4000},
            @{Text='appterm 60 /boot/PIPECHK.ELF';Delay=6000},@{Text='appterm 60 /boot/SPAWNCHK.ELF';Delay=5000},
            'appshell','help','path','hello','run echo "lookup ok" ""','run /boot/HELLO.ELF','not-installed-name','status',
            'exec /boot/CONFIG.TXT','echo shell-survived-exec','pipe exec /boot/STREAM.ELF -- exec /boot/HASH.ELF',
            'runio /boot/CONFIG.TXT /tmp/exec-copy /boot/EXEC.ELF /boot/CAT.ELF',
            'sha256 /boot/CONFIG.TXT','sha256 /tmp/exec-copy',
            'pipeio /boot/CONFIG.TXT /tmp/exec-hash exec /boot/CAT.ELF -- exec /boot/HASH.ELF','cat /tmp/exec-hash',
            'mkdir /data/bin','cp /boot/ECHO.ELF /data/bin/hello.elf','hello stored-precedence',
            'mv /data/bin/hello.elf /data/bin/speak','speak exact-name','run speak "quoted value" ""',
            'cp /boot/CONFIG.TXT /data/bin/hello.elf','hello invalid-override','rm /data/bin/hello.elf',
            'speak before-reboot','exec echo shell-replaced','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'PASS: failed exec retained the original process, stack and open handle; invalid buffers/argv/ELF/paths refused.',
            'PASS: exec retained PID/cwd/handle cursor and arguments; eight images used fresh stacks and preserved resources.',
            'PASS: exec preserves child identity/wait, repeated slot reuse, fault containment and original timeout across image changes.',
            'process=2 path=/boot/EXECCHK.ELF state=exited code=0',
            'PASS: pipelines binary FIFO/EOF, blocking/wakeup, reuse, invalid setup, broken pipe, faults/timeouts and cleanup.',
            'PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.',
            'run|exec PROGRAM [args]','/data/bin:/boot','lookup ok ',
            'shell: exec failed; current shell retained','shell-survived-exec',
            'a1f259d4365ed4320c377ce26f5c8c56dcdc9a89e7b641bfd8eabfbbeac86654',
            'stored-precedence','exact-name','quoted value ','before-reboot','shell-replaced',
            'path=/boot/echo.elf state=exited code=0','Open files: 0/16') 6
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        $cfgMatch=[regex]::Match($capture,'(?m)^([0-9a-f]{64})  /boot/CONFIG.TXT\r?$')
        $copyMatch=[regex]::Match($capture,'(?m)^([0-9a-f]{64})  /tmp/exec-copy\r?$')
        if (!$cfgMatch.Success -or !$copyMatch.Success -or $cfgMatch.Groups[1].Value -cne $copyMatch.Groups[1].Value -or
            ![regex]::IsMatch($capture,'cat /tmp/exec-hash\r?\n'+$cfgMatch.Groups[1].Value+'\r?\n')) { throw 'Exec did not preserve redirected cursors/streams' }
        if (![regex]::IsMatch($capture,'> status\r?\n127\r?\n') -and ![regex]::IsMatch($capture,'\$ status\r?\n127\r?\n')) { throw 'Missing-program status differs' }
        $shellCapture=$capture.Substring($capture.LastIndexOf('OurOS EL0 shell. Type help; exit returns to monitor.'))
        if ([regex]::Matches($shellCapture,[regex]::Escape('Hello from an OurOS EL0 application.')).Count -ne 2) { throw 'Program lookup precedence or invalid-candidate fallback differs' }
        if ([regex]::Matches($capture,'process=[012] path=/boot/EXECCHK.ELF state=exited code=0[^\r\n]*execs=8').Count -lt 4) { throw 'Three-slot exec or identity/count differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'speak cold-boot-lookup','pipe exec /boot/STREAM.ELF small -- exec /boot/HASH.ELF',
            'exec /data/bin/speak replaced-after-reboot','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'cold-boot-lookup','3e5718fea51a8f3f5baca61c77afab473c1810f8b9db330273b4011ce92c787e',
            'replaced-after-reboot','path=/data/bin/speak state=exited code=0','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Application exec wrote outside data banks' }
        Write-Output "PASS: $name exec/rollback/FD identity, three occupied slots, pipe/stream inheritance, original deadlines, lookup precedence and installed-program cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: exec acceptance uses regular scratch disks; delivery unchanged.';return
}
if ($Replace) {
    if ($layout.Version -ne 3) { throw 'Replacement acceptance requires the new v3 delivery' }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    $helloHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/HELLO.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    $replacementPass='PASS: RAM/data atomic replacement, binary/empty contents, consumed source, both leases, no-overwrite mv, aliases, readonly/cross-mount/directory refusal and syscall pointer checks.'
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="replace-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/REPLCHK.ELF';Delay=6000},'mkdir /data/apps','mkdir /data/staging',
            'appinstall /boot/HELLO.ELF /data/apps/live.elf','appshell','sha256 /data/apps/live.elf','exit',
            'appupdate /boot/CONFIG.TXT /data/apps/live.elf','appupdate /boot/HELLO.ELF /data/apps/missing.elf',
            'appshell','sha256 /data/apps/live.elf','stat /data/apps/missing.elf','exit',
            'appupdate /boot/SHELL.ELF /data/apps/live.elf','appinstall /boot/HELLO.ELF /data/apps/live.elf',
            'appterm 120 /data/apps/live.elf','sha256 /data/apps/live.elf','cd /data/apps',
            'write note.txt old-version','write save.tmp new-version','mv save.tmp note.txt','replace save.tmp note.txt',
            'cat note.txt','stat save.tmp',@{Text='runio - /data/staging/new.bin /boot/DATAGEN.ELF';Delay=6000},
            'write block.bin old-binary','replace /data/staging/new.bin block.bin','sha256 block.bin',
            'stat /data/staging/new.bin','sync','exit','datainfo',@{Text='dataexport';Delay=4000},
            'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            $replacementPass,'path=/boot/REPLCHK.ELF state=exited code=0',
            'app: installation refused:','app: path not found','app: already exists',
            'Updated validated ELF: /data/apps/live.elf',($helloHash+'  /data/apps/live.elf'),
            ($shellHash+'  /data/apps/live.elf'),'path=/data/apps/live.elf state=exited code=0',
            'shell: target exists; mv does not overwrite','new-version',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  block.bin','Open files: 0/16') 4
        $created=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($created,[regex]::Escape($helloHash+'  /data/apps/live.elf')).Count -ne 2) { throw 'Rejected ELF update changed the prior file' }
        . "$PSScriptRoot/data-backup.ps1"
        $replacementArchive=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-create.log") -Output $replacementArchive
        $published=Get-OurOSDataBackup $replacementArchive
        $publishedPaths=@($published.Entries | ForEach-Object { $_.Path } | Sort-Object)
        if (($publishedPaths -join ',') -cne 'apps,apps/block.bin,apps/live.elf,apps/note.txt,staging' -or
            @($published.Entries | Where-Object { $_.Path -ceq 'apps/live.elf' -and $_.SHA256 -ceq $shellHash }).Count -ne 1) { throw 'Published tree differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appterm 60 /data/apps/live.elf','sha256 /data/apps/live.elf','cat /data/apps/note.txt','sha256 /data/apps/block.bin',
            'stat /data/apps/save.tmp','stat /data/staging/new.bin','stat /data/apps/missing.elf','exit',
            'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            ($shellHash+'  /data/apps/live.elf'),'new-version','path=/data/apps/live.elf state=exited code=0',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/apps/block.bin','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Replacement changed boot/unused sectors' }
        Write-Output "PASS: $name validated ELF update/rejection, shell replacement, 8KiB binary, exported names/hashes and cold-boot execution; boot/tail unchanged."
    }
    . "$PSScriptRoot/data-format.ps1"
    foreach ($version in 1,2) {
        $compat=Join-Path $ProjectRoot ('build/usb-tests/replace-compat-v'+$version+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $compat
        $f=[IO.File]::Open($compat,[IO.FileMode]::Open,[IO.FileAccess]::Write)
        try { $f.Position=260096L*512;$bytes=New-OurOSDataPartition -Version $version;$f.Write($bytes,0,$bytes.Length) } finally { $f.Dispose() }
        Session $compat "replace-compat-v$version-create" 'cortex-a76' 'on' @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/REPLCHK.ELF';Delay=6000},'appinstall /boot/HELLO.ELF /data/live.elf',
            'appupdate /boot/ECHO.ELF /data/live.elf','appexec /data/live.elf legacy-update','pwd','fileinfo','timerstop','halt') @(
            ("OurOS snapshot v$version; files="),$replacementPass,'Updated validated ELF: /data/live.elf','legacy-update','Open files: 0/16') 2
        Session $compat "replace-compat-v$version-reopen" 'cortex-a76' 'on' @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appexec /data/live.elf legacy-reboot','pwd','fileinfo','timerstop','halt') @(
            ("OurOS snapshot v$version; files="),'legacy-reboot','path=/data/live.elf state=exited code=0','Open files: 0/16') 1
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: atomic replacement/update acceptance uses regular scratch disks; delivery unchanged.';return
}
if ($TreeRestore) {
    . "$PSScriptRoot/data-backup.ps1"
    if ($layout.Version -ne 3 -or !$ExpectedTreeBackup) { throw 'Tree restore requires v3 and -ExpectedTreeBackup' }
    $seed=Get-OurOSDataBackup $ExpectedTreeBackup
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="tree-restore-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit',@{Text=('datarestore '+$RestoreFile);Delay=4000},
            'datainfo','ls /data/archive','cat /data/archive/moved.txt','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/DIRCHK.ELF';Delay=6000},'appterm 60 /data/bin/cli.elf',
            'sha256 /data/bin/cli.elf','sha256 /data/archive/large.bin','exit',@{Text='dataexport';Delay=4000},
            'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'Data restore committed and read back;','bank=1 generation=2 valid_banks=2','persistent-tree',
            'PASS: persistent nested mkdir/cwd/list, sibling names, binary move/hash, open-file and cwd pins, path/type/nonempty refusal and cleanup.',
            'path=/data/bin/cli.elf state=exited code=0',($shellHash+'  /data/bin/cli.elf'),
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/archive/large.bin','Open files: 0/16') 2
        $restoredArchive=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-create.log") -Output $restoredArchive
        $restored=Get-OurOSDataBackup $restoredArchive
        if ([Convert]::ToBase64String($restored.Raw[32..63]) -cne [Convert]::ToBase64String($seed.Raw[32..63])) { throw 'Restored tree/selftest cleanup changed the complete source payload' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appterm 60 /data/bin/cli.elf','sha256 /data/bin/cli.elf','sha256 /data/bin/large.bin','cat /data/archive/moved.txt',
            'exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'path=/data/bin/cli.elf state=exited code=0',($shellHash+'  /data/bin/cli.elf'),'persistent-tree',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/bin/large.bin','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Restore touched boot/unused sectors' }
        Write-Output "PASS: $name complete tree archive -> image -> restore, DIRCHK on populated tree, exact full payload and nested ELF cold boot."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery changed' }
    Write-Output 'PASS: tree restoration used regular scratch disks; delivery unchanged.';return
}
if ($Directories) {
    . "$PSScriptRoot/data-backup.ps1"
    if ($layout.Version -ne 3) { throw 'Directory test requires v3' }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512;$tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="dirs-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','usbcheck','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/DIRCHK.ELF';Delay=6000},'mkdir /data/projects','mkdir /data/projects/demo','mkdir /data/archive',
            'write /data/projects/demo/note.txt persistent-tree','ls /data','stat /data/projects/demo','appshell','cd /data/projects/demo',
            'pwd','cat note.txt','write /data/archive/note.txt sibling','ls /data/archive','mv note.txt /data/archive/moved.txt',
            'rmdir .','cd /','rmdir /data/projects/demo','mkdir /data/apps','mkdir /data/bin',
            @{Text='runio - /data/archive/large.bin /boot/DATAGEN.ELF';Delay=7000},'sha256 /data/archive/large.bin',
            'stat /data/archive/large.bin','cp /data/archive/large.bin /data/bin/large.bin','sha256 /data/bin/large.bin',
            'rmdir /data/archive','stat /data/projects','sync','exit',
            'appinstall /boot/SHELL.ELF /data/apps/shell.elf','mv /data/apps/shell.elf /data/bin/cli.elf',
            'appterm 60 /data/bin/cli.elf','sha256 /data/bin/cli.elf','cat /data/archive/moved.txt','exit',
            @{Text='dataexport';Delay=4000},'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'OurOS snapshot v3; nodes=16 max_file_bytes=8192',
            'PASS: persistent nested mkdir/cwd/list, sibling names, binary move/hash, open-file and cwd pins, path/type/nonempty refusal and cleanup.',
            'type=directory bytes=0 path=/data/projects/demo','user:/data/projects/demo$',
            'persistent-tree','shell: operation failed','type=file bytes=8192 path=/data/archive/large.bin',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/archive/large.bin',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/bin/large.bin',
            ($shellHash+'  /data/bin/cli.elf'),'path=/data/bin/cli.elf state=exited code=0','Open files: 0/16') 3
        $treeArchive=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-create.log") -Output $treeArchive
        $tree=Get-OurOSDataBackup $treeArchive
        $paths=@($tree.Entries | ForEach-Object { $_.Path } | Sort-Object)
        if (($paths -join ',') -cne 'apps,archive,archive/large.bin,archive/moved.txt,archive/note.txt,bin,bin/cli.elf,bin/large.bin,projects' -or
            @($tree.Entries | Where-Object { $_.Path -ceq 'bin/cli.elf' -and $_.SHA256 -ceq $shellHash }).Count -ne 1) { throw 'Independent exported tree differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','ls /data/archive','cat /data/archive/moved.txt',
            'mmuinit','irqinit','timerstart','appterm 60 /data/bin/cli.elf','cd /data/bin','pwd','sha256 cli.elf','sha256 large.bin',
            'ls /data/archive','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'f 15 moved.txt','persistent-tree','path=/data/bin/cli.elf state=exited code=0',($shellHash+'  cli.elf'),
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  large.bin','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Directory operations wrote outside banks' }
        Write-Output "PASS: $name nested paths/EL0/cwd/leases, 8192-byte files, independent exported graph and nested ELF cold boot; boot/unused sectors unchanged."
    }
    . "$PSScriptRoot/data-format.ps1"
    foreach ($version in 1,2) {
        $compat=Join-Path $ProjectRoot ('build/usb-tests/dirs-compat-v'+$version+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $compat
        $f=[IO.File]::Open($compat,[IO.FileMode]::Open,[IO.FileAccess]::Write)
        try { $f.Position=260096L*512;$bytes=New-OurOSDataPartition -Version $version;$f.Write($bytes,0,$bytes.Length) } finally { $f.Dispose() }
        Session $compat "dirs-compat-v$version-create" 'cortex-a76' 'on' @('storageinit','datainit','mkdir /data/not-supported',
            'mmuinit','irqinit','timerstart',@{Text='appterm 120 /boot/MVCHK.ELF';Delay=5000},'write /data/old legacy-compatible',
            'mv /data/old /data/new','stat /data/new','sync','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            ("OurOS snapshot v$version; files="),'fs: unsupported filesystem format',
            'PASS: RAM/data binary rename, relative aliases, no overwrite, open-handle leases, readonly/cross-mount/directory refusal and syscall pointer checks.',
            'type=file bytes=17 path=/data/new','Open files: 0/16') 1
        Session $compat "dirs-compat-v$version-reopen" 'cortex-a76' 'on' @('storageinit','datainit','cat /data/new','stat /data/old','pwd','halt') @(
            ("OurOS snapshot v$version; files="),'legacy-compatible','fs: path not found') 0
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
    Write-Output 'PASS: directory tests used regular scratch disks; delivery unchanged.';return
}
if ($Rename) {
    . "$PSScriptRoot/data-backup.ps1"
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512
    $tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $h=[Security.Cryptography.SHA256]::Create()
    try { $digest=([BitConverter]::ToString($h.ComputeHash([Text.Encoding]::ASCII.GetBytes('move-from-el0')))).Replace('-','').ToLowerInvariant() } finally { $h.Dispose() }
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="rename-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 60 /boot/MVCHK.ELF';Delay=4000},'write /data/old.txt kernel-move','stat /data/old.txt',
            'mv /data/old.txt /data/new.txt','cat /data/old.txt','stat /data/new.txt','write /data/keep.txt keep',
            'mv /data/keep.txt /data/new.txt','cat /data/keep.txt','appshell','cd /data','write shell-old move-from-el0',
            'stat shell-old','sha256 shell-old','mv ./shell-old ../data//shell-new','sha256 shell-new','stat shell-new',
            'mv shell-new shell-new','mv keep.txt shell-new','mv shell-new /tmp/cross','mv /tmp /tmp/moved-dir',
            'mv /boot/CONFIG.TXT /data/no-boot','write /tmp/ram-old ram-move','mkdir /tmp/mv-dir','mv /tmp/ram-old /tmp/mv-dir/ram-new',
            'stat /tmp/mv-dir/ram-new','stat /tmp/mv-dir','rm /tmp/mv-dir/ram-new','rmdir /tmp/mv-dir','sync','exit',
            'appinstall /boot/SHELL.ELF /data/shell.elf','mv /data/shell.elf /data/cli.elf',
            'appterm 60 /data/cli.elf','sha256 /data/cli.elf','stat /data/cli.elf','exit',
            @{Text='dataexport';Delay=4000},'pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'PASS: RAM/data binary rename, relative aliases, no overwrite, open-handle leases, readonly/cross-mount/directory refusal and syscall pointer checks.',
            'type=file bytes=11 path=/data/old.txt','fs: path not found','fs: already exists','type=file bytes=11 path=/data/new.txt',
            'type=file bytes=13 path=shell-old',($digest+'  shell-old'),($digest+'  shell-new'),
            'shell: target exists; mv does not overwrite','shell: mv requires the same filesystem','shell: mv accepts regular files only','shell: read-only filesystem',
            'type=file bytes=8 path=/tmp/mv-dir/ram-new','type=directory bytes=0 path=/tmp/mv-dir',
            ($shellHash+'  /data/cli.elf'),'path=/data/cli.elf state=exited code=0','-----END OUROS DATA BACKUP V1-----','Open files: 0/16') 3
        $renameArchive=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-create.log") -Output $renameArchive
        $decoded=Get-OurOSDataBackup $renameArchive
        $actual=@($decoded.Entries | ForEach-Object { $_.Name } | Sort-Object)
        if (($actual -join ',') -cne 'cli.elf,keep.txt,new.txt,shell-new' -or
            @($decoded.Entries | Where-Object { $_.Name -ceq 'shell-new' -and $_.SHA256 -ceq $digest }).Count -ne 1 -or
            @($decoded.Entries | Where-Object { $_.Name -ceq 'cli.elf' -and $_.SHA256 -ceq $shellHash }).Count -ne 1) { throw 'Independent renamed-file export differs' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','cat /data/new.txt','stat /data/old.txt',
            'mmuinit','irqinit','timerstart','appterm 60 /data/cli.elf','sha256 /data/shell-new','sha256 /data/cli.elf',
            'stat /data/shell-new','cat /data/keep.txt','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'kernel-move','fs: path not found','path=/data/cli.elf state=exited code=0',($digest+'  /data/shell-new'),
            ($shellHash+'  /data/cli.elf'),'type=file bytes=13 path=/data/shell-new','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Rename wrote outside data banks' }
        Write-Output "PASS: $name monitor/EL0 rename+stat, leases/errors, exact exported names/digests, moved-shell cold boot and unchanged boot/unused regions."
    }
    $legacySource=(Resolve-Path -LiteralPath $LegacyImage).Path;$legacyHash=(Get-FileHash -LiteralPath $legacySource -Algorithm SHA256).Hash
    $legacy=Join-Path $ProjectRoot ('build/usb-tests/rename-v1-'+[guid]::NewGuid().ToString('N')+'.img');Copy-Item -LiteralPath $legacySource -Destination $legacy
    Session $legacy 'rename-v1-create' 'cortex-a76' 'on' @('storageinit','datainit','write /data/old legacy-move',
        'mv /data/old /data/new','stat /data/new','sync','pwd','halt') @('OurOS snapshot v1','type=file bytes=11 path=/data/new') 0
    Session $legacy 'rename-v1-reopen' 'cortex-a76' 'on' @('storageinit','datainit','cat /data/new','stat /data/old','pwd','halt') @('legacy-move','fs: path not found') 0
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original -or (Get-FileHash -LiteralPath $legacySource -Algorithm SHA256).Hash -ne $legacyHash) { throw 'Original image changed' }
    Write-Output 'PASS: legacy v1 rename/reopen without conversion; all guest writes used regular scratch files.';return
}
if ($Backup) {
    . "$PSScriptRoot/data-backup.ps1"
    if ($layout.Version -ne 2) { throw 'Recovery acceptance requires v2 image' }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512
    $tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="backup-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','usbcheck','datainit',
            'datarestore /boot/MISSING.BAK',@{Text=('datarestore '+$RestoreFile);Delay=3500},'datainfo',
            'cat /data/restore.txt',('datarestore '+$RestoreFile),@{Text='dataexport';Delay=4000},
            'mmuinit','irqinit','timerstart','appinstall /boot/SHELL.ELF /data/shell.elf','appterm 60 /data/shell.elf',
            'sha256 /data/pattern.bin','sha256 /data/shell.elf','run /boot/HELLO.ELF','sync','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'Data restore refused/failed: path not found','Data restore committed and read back;',
            'bank=1 generation=2 valid_banks=2','OurOS verified backup restore.',
            'requires empty healthy /data','-----END OUROS DATA BACKUP V1-----',
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/pattern.bin',
            ($shellHash+'  /data/shell.elf'),'Open files: 0/16') 1
        $archive=Join-Path $out ($name+'-'+[guid]::NewGuid().ToString('N')+'.bak')
        & "$PSScriptRoot/convert-data-backup.ps1" -Log (Join-Path $out "$name-create.log") -Output $archive
        $decoded=Get-OurOSDataBackup $archive
        if ($decoded.Version -ne 2 -or $decoded.Generation -ne 2 -or $decoded.Entries.Count -ne 2 -or
            @($decoded.Entries | Where-Object { $_.Name -ceq 'pattern.bin' -and $_.Bytes -eq 8192 -and $_.SHA256 -ceq 'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46' }).Count -ne 1) { throw 'Actual guest serial export differs from restored snapshot' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','cat /data/restore.txt','mmuinit','irqinit','timerstart',
            'appterm 60 /data/shell.elf','sha256 /data/pattern.bin','sha256 /data/shell.elf','exit','pwd','fileinfo','heaptest','pagetest','timerstop','halt') @(
            'OurOS verified backup restore.','path=/data/shell.elf state=exited code=0',($shellHash+'  /data/shell.elf'),
            'dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46  /data/pattern.bin','Open files: 0/16') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Restore wrote outside data banks' }
        Write-Output "PASS: $name restore/export conversion, overwrite refusal, stored shell cold boot and unchanged boot/unused regions."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Source delivery image changed' }
    Write-Output 'PASS: backup QEMU checks used regular scratch files; delivery unchanged.';return
}
if ($Data2) {
    if ($layout.Version -ne 2) { throw 'Capacity test requires v2 delivery image' }
    $shellBytes=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot 'build/apps/SHELL.ELF'))
    $shellHash=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'build/apps/SHELL.ELF') -Algorithm SHA256).Hash.ToLowerInvariant()
    $pattern=New-Object byte[] 8192;for ($i=0;$i -lt 8192;$i++) { $pattern[$i]=[byte]($i%256) }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $binaryHash=([BitConverter]::ToString($sha.ComputeHash($pattern))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512
    $tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="data2-$($case[0])-$($case[1])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        Session $disk "$name-create" $case[0] $case[1] @('storageinit','usbcheck','datainit','mmuinit','irqinit','timerstart',
            @{Text='appterm 300 /boot/DATACAP.ELF';Delay=30000},'usbcheck','storageinfo','appinstall /boot/SHELL.ELF /data/shell.elf',
            'appterm 60 /data/shell.elf','sha256 /boot/SHELL.ELF','sha256 /data/shell.elf',
            @{Text='runio - /data/large.bin /boot/DATAGEN.ELF';Delay=9000},'sha256 /data/large.bin','ls /data',
            'cp /data/large.bin /tmp/too-large','run /boot/HELLO.ELF','sync','exit','pwd','fileinfo','heaptest','pagetest','datainfo',
            'timerstop','sha256 /data/large.bin','runio /data/large.bin - /boot/CAT.ELF','timerstart','appshell','echo shell-after-hint','exit',
            'fileinfo','heaptest','pagetest','timerstop','echo data2-monitor-ok','halt') @(
            'OurOS snapshot v2; files=16 max_file_bytes=8192',
            'PASS: USB command/control ring reuse, device identity and sector zero unchanged; no disk writes.',
            'PASS: v2 8192-byte binary file/copy/EOF, 16 slots, overflow preservation, RAM 4096-byte bound and cleanup.',
            'Installed validated ELF: /data/shell.elf bytes=',($shellHash+'  /boot/SHELL.ELF'),($shellHash+'  /data/shell.elf'),
            ($binaryHash+'  /data/large.bin'),'f 8192 large.bin','path=/data/shell.elf state=exited code=0',
            'Shell command: use appshell and wait for user:/$', 'shell-after-hint','Open files: 0/16','data2-monitor-ok') 3
        $ringCapture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ([regex]::Matches($ringCapture,'PASS: USB command/control ring reuse').Count -ne 2 -or
            $ringCapture -notmatch 'command_wraps=[1-9][0-9]* control_wraps=[1-9][0-9]*') { throw "$name did not prove repeated command/control ring reuse" }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart',
            'appterm 60 /data/shell.elf','sha256 /data/shell.elf','sha256 /data/large.bin','run /boot/HELLO.ELF','echo persisted-shell-live','exit',
            'pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','echo data2-reopened','halt') @(
            ($shellHash+'  /data/shell.elf'),($binaryHash+'  /data/large.bin'),'persisted-shell-live',
            'path=/data/shell.elf state=exited code=0','Open files: 0/16','data2-reopened') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'V2 wrote outside data banks' }
        $file=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
        try { $file.Position=260096L*512;if ($file.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Short data partition' } } finally { $file.Dispose() }
        $banks=@();$sha=[Security.Cryptography.SHA256]::Create()
        try {
            foreach ($bank in 0,1) {
                $at=512+$bank*$layout.BankSectors*512;$payload=$at+512
                if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne $layout.Magic -or [BitConverter]::ToUInt32($bytes,$at+8) -ne 2 -or
                    [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt32($bytes,$at+24) -ne $layout.PayloadBytes -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$payload,$layout.PayloadBytes)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'V2 bank hash/identity failure' }
                $banks+=[pscustomobject]@{Payload=$payload;Generation=[BitConverter]::ToUInt64($bytes,$at+16)}
            }
            $ordered=@($banks | Sort-Object -Property Generation -Descending)
            if ($ordered[0].Generation -ne ($ordered[1].Generation+1)) { throw 'V2 generation ordering differs' }
            foreach ($rank in 0,1) {
                $payload=$ordered[$rank].Payload;$fileAt=$payload+$layout.DirectoryBytes;$binaryAt=$fileAt+$layout.FileBytes
                $expectedBytes=8192-$rank*512
                if ([Text.Encoding]::ASCII.GetString($bytes,$payload,32).TrimEnd([char]0) -cne 'shell.elf' -or
                    [BitConverter]::ToUInt32($bytes,$payload+32) -ne $shellBytes.Length -or
                    [Text.Encoding]::ASCII.GetString($bytes,$payload+64,32).TrimEnd([char]0) -cne 'large.bin' -or
                    [BitConverter]::ToUInt32($bytes,$payload+96) -ne $expectedBytes) { throw 'V2 persisted file metadata differs' }
                for ($i=0;$i -lt $shellBytes.Length;$i++) { if ($bytes[$fileAt+$i] -ne $shellBytes[$i]) { throw 'Installed shell bytes differ' } }
                for ($i=0;$i -lt $expectedBytes;$i++) { if ($bytes[$binaryAt+$i] -ne [byte]($i%256)) { throw 'V2 persisted binary bytes differ' } }
                foreach ($entry in 2..15) { if ($bytes[$payload+$entry*64]) { throw 'Capacity fixtures were not cleaned up' } }
            }
        } finally { $sha.Dispose() }
        Write-Output "PASS: $name v2 capacity, installed shell cold boot, 8192 binary bytes, monitor hints and independent disk integrity."
    }
    $legacy=(Resolve-Path -LiteralPath $LegacyImage).Path;$legacyLayout=Get-OurOSDataLayout $legacy
    if ($legacyLayout.Version -ne 1) { throw 'Compatibility test requires v1 archive' }
    $legacyHash=(Get-FileHash -LiteralPath $legacy -Algorithm SHA256).Hash
    $legacyPrefix=Range-Hash $legacy 0 (260097L*512);$legacyTailAt=(260096L+$legacyLayout.UsedSectors)*512
    $legacyTail=Range-Hash $legacy $legacyTailAt (134217728L-$legacyTailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="data1-compat-$($case[0])-$($case[1])";$disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $legacy -Destination $disk
        Session $disk $name $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','write /data/legacy.txt legacy-ok',
            'appinstall /boot/SHELL.ELF /data/too-large.elf','sync','appshell','cat /data/legacy.txt','exit','pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','echo legacy-compatible','halt') @(
            'OurOS snapshot v1; files=8 max_file_bytes=4096','app: file too large for this operation','legacy-ok','Open files: 0/16','legacy-compatible') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $legacyPrefix -or (Range-Hash $disk $legacyTailAt (134217728L-$legacyTailAt)) -ne $legacyTail) { throw 'V1 layout was modified or write escaped old banks' }
        Write-Output "PASS: $name old v1 mount/write/read/limits retained without conversion."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original -or (Get-FileHash -LiteralPath $legacy -Algorithm SHA256).Hash -ne $legacyHash) { throw 'Delivery/archive image changed' }
    Write-Output 'PASS: v1/v2 tests used only regular scratch disks; delivery and archive unchanged.'
    return
}
if ($Pipes) {
    $content="hello from OurOS`n";$hash=[Security.Cryptography.SHA256]::Create()
    try { $digest=([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::ASCII.GetBytes($content)))).Replace('-','').ToLowerInvariant() } finally { $hash.Dispose() }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512
    $tail=Range-Hash $source $tailAt (134217728L-$tailAt)
    foreach ($case in @(@('cortex-a53','off'),@('cortex-a76','on'))) {
        $name="pipes-$($case[0])-$($case[1])"
        $disk=Join-Path $ProjectRoot ('build/usb-tests/'+$name+'-'+[guid]::NewGuid().ToString('N')+'.img')
        Copy-Item -LiteralPath $source -Destination $disk
        $actions=@('storageinit','datainit','mmuinit','irqinit','timerstart',@{Text='appterm 30 /boot/PIPECHK.ELF';Delay=6000},
            'apprun /boot/CAPCHK.ELF /boot/CAPCHK.ELF /boot/CAPCHK.ELF','appshell',
            'runio - /data/greeting.txt /boot/ECHO.ELF "hello from OurOS"','runio /data/greeting.txt /data/ctxt /boot/CAT.ELF',
            'sha256 /data/copy.txt','runio /data/copy.txt - /boot/CAT','sha256 /data/ctxt','runio /data/ctxt - /boot/CAT.ELF',
            'pipe /boot/ECHO.ELF "pipe-ok" -- /boot/CAT.ELF','pipe /boot/STREAM.ELF -- /boot/HASH.ELF',
            'pipeio /data/ctxt /data/piped.txt /boot/CAT.ELF -- /boot/CAT.ELF','sha256 /data/piped.txt',
            'pipeio - /tmp/digest /boot/STREAM.ELF -- /boot/HASH.ELF','cat /tmp/digest','rm /tmp/digest',
            'pipe /boot/ECHO.ELF -- /boot/CAT.ELF -- /boot/CAT.ELF','pipe /boot/MISSING.ELF -- /boot/HASH.ELF',
            'pipe /boot/STREAM.ELF -- /boot/STREAM.ELF exit','status','pipe /boot/IORAW.ELF fault -- /boot/HASH.ELF','status',
            'pipe /boot/STREAM.ELF -- /boot/SPIN.ELF',@{Text=[string][char]3;Raw=$true;Delay=600},'status','run /boot/HELLO.ELF',
            'sync','exit','pwd','appterm 2 /boot/SHELL.ELF',@{Text='pipe /boot/STREAM.ELF -- /boot/SPIN.ELF';Delay=2800},
            'fileinfo','heaptest','pagetest','heapinfo','datainfo','timerstop','echo pipes-monitor-ok','halt')
        Session $disk "$name-create" $case[0] $case[1] $actions @(
            'PASS: pipelines binary FIFO/EOF, blocking/wakeup, reuse, invalid setup, broken pipe, faults/timeouts and cleanup.',
            'PASS: full process table refuses child creation.','shell: file not found: /data/copy.txt','App load failed: path not found: /boot/CAT',
            ($digest+'  /data/ctxt'),($digest+'  /data/piped.txt'),'pipe-ok',
            'a1f259d4365ed4320c377ce26f5c8c56dcdc9a89e7b641bfd8eabfbbeac86654',
            'shell: use pipe <ELF> [args] -- <ELF> [args]','pipeline exit=0','pipeline exit=74','pipeline exit=125','pipeline exit=130',
            'path=/boot/SHELL.ELF state=timeout code=124','Open files: 0/16','pages=2 objects=3 capacity_bytes=544','generation=8','pipes-monitor-ok') 4
        $capture=Get-Content -Raw -LiteralPath (Join-Path $out "$name-create.log")
        if ($capture.Contains([string][char]27) -or !$capture.Contains(': .bad')) { throw 'Untrusted diagnostic path was not safely displayed' }
        if (![regex]::IsMatch($capture,'Pipelines: started=[1-9][0-9]* completed=[1-9][0-9]* written=[0-9]+ read=[0-9]+ reader_waits=[1-9][0-9]* writer_waits=[1-9][0-9]*')) { throw 'Pipe blocking paths were not exercised' }
        Session $disk "$name-reopen" $case[0] $case[1] @('storageinit','datainit','mmuinit','irqinit','timerstart','appshell',
            'sha256 /data/greeting.txt','sha256 /data/ctxt','sha256 /data/piped.txt',
            'pipeio /data/piped.txt - /boot/CAT.ELF -- /boot/CAT.ELF','exit','pwd','fileinfo','heaptest','pagetest','datainfo','timerstop','echo pipes-reopened','halt') @(
            ($digest+'  /data/greeting.txt'),($digest+'  /data/ctxt'),($digest+'  /data/piped.txt'),
            'hello from OurOS','pipeline exit=0','generation=8','Open files: 0/16','pipes-reopened') 1
        if ((Range-Hash $disk 0 (260097L*512)) -ne $prefix -or (Range-Hash $disk $tailAt (134217728L-$tailAt)) -ne $tail) { throw 'Unexpected pipeline writes outside data banks' }
        $file=[IO.File]::OpenRead($disk);$bytes=New-Object byte[] (2048*512)
        try { $file.Position=260096L*512;if ($file.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Short data partition' } } finally { $file.Dispose() }
        $sha=[Security.Cryptography.SHA256]::Create()
        try {
            foreach ($bank in 0,1) {
                $at=512+$bank*$layout.BankSectors*512;$payload=$at+512
                if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne $layout.Magic -or [BitConverter]::ToUInt32($bytes,$at+8) -ne $layout.Version -or
                    [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt64($bytes,$at+16) -ne (7+$bank) -or
                    [BitConverter]::ToUInt32($bytes,$at+24) -ne $layout.PayloadBytes -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$payload,$layout.PayloadBytes)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Pipeline snapshot metadata/hash failure' }
                foreach ($entry in 0,1,2) {
                    $record=$payload+$entry*64;$data=$payload+$layout.DirectoryBytes+$entry*$layout.FileBytes
                    $expectedName=@('greeting.txt','ctxt','piped.txt')[$entry]
                    $expected=if ($entry -eq 2 -and !$bank) { '' } else { $content }
                    if ([Text.Encoding]::ASCII.GetString($bytes,$record,32).TrimEnd([char]0) -cne $expectedName -or
                        [BitConverter]::ToUInt32($bytes,$record+32) -ne $expected.Length -or
                        [Text.Encoding]::ASCII.GetString($bytes,$data,$expected.Length) -cne $expected) { throw 'Pipeline persisted file bytes differ' }
                }
                foreach ($entry in 3..($layout.Files-1)) { if ($bytes[$payload+$entry*64]) { throw 'Rejected pipeline created an extra persistent file' } }
            }
        } finally { $sha.Dispose() }
        Write-Output "PASS: $name concurrent pipes, diagnostics, EOF/EPIPE/blocking/cancel, cold boot and independent disk integrity."
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
    Write-Output 'PASS: pipeline tests used regular scratch disks; delivery image unchanged.'
    return
}
if ($Stdio) {
    function Digest([byte[]]$Bytes) {
        $hasher=[Security.Cryptography.SHA256]::Create()
        try { return ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
    }
    $content="redirected output`n";$digest=Digest ([Text.Encoding]::ASCII.GetBytes($content))
    $binary=New-Object byte[] 512;for ($i=0;$i -lt $binary.Length;$i++) { $binary[$i]=[byte]($i%256) }
    $full=New-Object byte[] 4096;for ($i=0;$i -lt $full.Length;$i++) { $full[$i]=[byte]($i%256) }
    $prefix=Range-Hash $source 0 (260097L*512);$tailAt=(260096L+$layout.UsedSectors)*512
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
                $at=512+$bank*$layout.BankSectors*512;$payload=$at+512
                if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne $layout.Magic -or [BitConverter]::ToUInt32($bytes,$at+8) -ne $layout.Version -or
                    [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt64($bytes,$at+16) -ne (5+$bank) -or
                    [BitConverter]::ToUInt32($bytes,$at+24) -ne $layout.PayloadBytes -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                    [Convert]::ToBase64String($sha.ComputeHash($bytes,$payload,$layout.PayloadBytes)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Stdio snapshot metadata/hash failure' }
                foreach ($entry in 0,1) {
                    $record=$payload+$entry*64;$data=$payload+$layout.DirectoryBytes+$entry*$layout.FileBytes
                    $expectedName=if ($entry -eq 0) { 'stdout.txt' } else { 'stdcopy.txt' }
                    $expected=if ($entry -eq 1 -and !$bank) { '' } else { $content }
                    if ([Text.Encoding]::ASCII.GetString($bytes,$record,32).TrimEnd([char]0) -cne $expectedName -or
                        [BitConverter]::ToUInt32($bytes,$record+32) -ne $expected.Length -or
                        [Text.Encoding]::ASCII.GetString($bytes,$data,$expected.Length) -cne $expected) { throw 'Stdio persisted bytes/length/name differ' }
                }
                foreach ($entry in 2..($layout.Files-1)) { if ($bytes[$payload+$entry*64]) { throw 'Rejected redirection created an extra persistent file' } }
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
        'children_started=12 children_reaped=12 last_child_code=124 cleanup=restored',
        'Parent exits with live child;','child exit=0','child exit=125','child exit=130','resumed-parent','Open files: 0/16',
        'PASS: child readline before parent wait blocks until terminal handoff.') 4
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
    return
}
$prefix=Range-Hash $source 0 (260097L*512)
$tailAt=(260096L+$layout.UsedSectors)*512
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
        'shell: invalid quoting, expansion or argument bounds','shell: input rejected; command not executed','edit-ok','cleared',
        'PASS: terminal pointer/capacity checks and blocking line delivery.',
        'path=/boot/TTYCHK.ELF state=cancelled code=130','path=/boot/SPIN.ELF state=cancelled code=130',
        'path=/boot/TTYCHK.ELF state=timeout code=124','path=/boot/SPIN.ELF state=cancelled code=75',
        'Usage: appterm <1..300 seconds>','ELF rejected: unsupported ELF header','Open files: 0/16',
        'pages=2 objects=3 capacity_bytes=544','generation=3','shell-monitor-ok',($contentHash+'  /data/shell.txt'),
        'PASS: binary copy, zero/4096-byte bounds, no overwrite, SHA256 vectors, leases and pointer checks.',
        'PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.',
        'PASS: full process table refuses child creation.','Parent exits with live child;',
        'quoted child','children_started=12 children_reaped=12 last_child_code=124 cleanup=restored',
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
            $at=512+$bank*$layout.BankSectors*512
            if ([Text.Encoding]::ASCII.GetString($bytes,$at,8) -cne $layout.Magic -or [BitConverter]::ToUInt32($bytes,$at+8) -ne $layout.Version -or
                [BitConverter]::ToUInt32($bytes,$at+12) -ne $bank -or [BitConverter]::ToUInt32($bytes,$at+24) -ne $layout.PayloadBytes -or
                [Convert]::ToBase64String($sha.ComputeHash($bytes,$at,64)) -cne [Convert]::ToBase64String($bytes[($at+64)..($at+95)]) -or
                [Convert]::ToBase64String($sha.ComputeHash($bytes,$at+512,$layout.PayloadBytes)) -cne [Convert]::ToBase64String($bytes[($at+32)..($at+63)])) { throw 'Snapshot hash/header failure' }
            $g=[BitConverter]::ToUInt64($bytes,$at+16);$content=if ($bank -eq 0) { 'saved' } else { 'persistent shell data' }
            $payload=$at+512
            if ($g -ne (3-$bank) -or [Text.Encoding]::ASCII.GetString($bytes,$payload,32).TrimEnd([char]0) -cne 'shell.txt' -or
                [BitConverter]::ToUInt32($bytes,$payload+32) -ne $content.Length -or
                [Text.Encoding]::ASCII.GetString($bytes,$payload+$layout.DirectoryBytes,$content.Length) -cne $content) { throw 'Independent shell file bytes/generation differ' }
            foreach ($i in 1..($layout.Files-1)) { if ($bytes[$payload+$i*64]) { throw 'Rejected shell line created an unexpected file' } }
        }
    } finally { $sha.Dispose() }
    Write-Output "PASS: $name interactive editing, syscall bounds, mutations, Ctrl+C/EOF/overflow/deadline, cleanup and cold-boot data; independent snapshot and untouched boot checks."
}
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $original) { throw 'Delivery image changed' }
Write-Output 'PASS: shell tests used regular scratch disks; source image unchanged.'
