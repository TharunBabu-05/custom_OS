[CmdletBinding()]
param([switch]$Handoff,[switch]$Enumerate,[switch]$Keyboard,[switch]$Storage,[switch]$WifiFiles,[switch]$Hotplug,[switch]$BootWithKeyboard,
      [string]$StorageImage='build/rpi5-header/OurOS-Pi5-header-experimental-sd.img')
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build virt or handoff-test first.' }
$start=New-Object System.Diagnostics.ProcessStartInfo
$start.FileName=$QemuExe
$virt=if ($Handoff) { 'on' } else { 'off' }
$storageOptions=''
if (@($Enumerate,$Keyboard,$Storage).Where({ $_ }).Count -gt 1) { throw 'Choose -Enumerate, -Keyboard or -Storage' }
if ($WifiFiles -and !$Storage) { throw '-WifiFiles requires -Storage' }
if ($Hotplug -and (!$Storage -or $WifiFiles)) { throw '-Hotplug requires -Storage alone' }
if ($BootWithKeyboard -and (!$Storage -or $Hotplug -or $WifiFiles)) { throw '-BootWithKeyboard requires -Storage alone' }
if ($Enumerate -or $Storage) {
    $storagePath=(Resolve-Path -LiteralPath $StorageImage).Path
    $storageHash=(Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash
    $storageOptions=" -drive file=`"$storagePath`",if=none,format=raw,readonly=on,id=usbdisk -device usb-storage,bus=xhci.0,drive=usbdisk"
}
if ($Keyboard) { $storageOptions=' -device usb-kbd,bus=xhci.0' }
if ($BootWithKeyboard) {
    $storageOptions=$storageOptions.Replace('-device usb-storage,bus=xhci.0,','-device usb-storage,bus=xhci.0,port=2,')
    $storageOptions+=' -device usb-kbd,bus=xhci.0,port=1'
}
$xhciDevice=if ($Enumerate -or $Keyboard -or $Storage) { 'qemu-xhci,id=xhci,p3=0' } else { 'qemu-xhci,id=xhci' }
$start.Arguments="-machine virt-9.2,virtualization=$virt,secure=off -cpu cortex-a76 -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device $xhciDevice -kernel `"$kernel`"$storageOptions"
if ($Hotplug) {
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    $listener.Start()
    try { $qmpPort=$listener.LocalEndpoint.Port } finally { $listener.Stop() }
    $start.Arguments+=" -qmp tcp:127.0.0.1:${qmpPort},server=on,wait=off"
}
$start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$process=New-Object System.Diagnostics.Process; $process.StartInfo=$start
$started=$false
$qmp=$null
function Invoke-Qmp([hashtable]$Request,[bool]$WaitForRemoval=$false) {
    $writer.WriteLine(($Request | ConvertTo-Json -Compress -Depth 4))
    $returned=$false
    $removed=$false
    do {
        $line=$reader.ReadLine()
        if (!$line) { throw 'QMP disconnected' }
        $reply=$line | ConvertFrom-Json
        if ($reply.error) { throw $reply.error.desc }
        if ($reply.PSObject.Properties['return']) { $returned=$true }
        if ($reply.event -eq 'DEVICE_DELETED' -and $reply.data.device -eq 'test-keyboard') { $removed=$true }
    } until ($returned -and (!$WaitForRemoval -or $removed))
}
try {
    if (!$process.Start()) { throw 'Cannot start QEMU' }; $started=$true
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 1000
    if ($Hotplug) {
        if ($process.HasExited) { throw $stderr.Result }
        $qmp=[Net.Sockets.TcpClient]::new('127.0.0.1',$qmpPort)
        $net=$qmp.GetStream()
        $net.ReadTimeout=4000
        $reader=[IO.StreamReader]::new($net)
        $writer=[IO.StreamWriter]::new($net,[Text.UTF8Encoding]::new($false))
        $writer.AutoFlush=$true
        if (!(($reader.ReadLine() | ConvertFrom-Json).QMP)) { throw 'Unexpected QMP greeting' }
        Invoke-Qmp @{execute='qmp_capabilities'}
    }
    $usbCommand=if ($Enumerate -or $Keyboard) { 'usbenum' } else { 'usbtest' }
    $commands=@('usbdevices',$usbCommand,'usbdevices',$usbCommand,'uptime','cat /etc/board','pagetest','heaptest','heapinfo','fsinfo','echo usb-ring-monitor-ok','halt')
    if ($Storage) { $commands=@('usbdevices','storageinit','usbdevices','storageinit','storageinfo','ls /boot','cat /boot/CONFIG.TXT','storagetest','usbdevices','storageinfo','cat /boot/SOURCE.TXT','uptime','cat /etc/board','pagetest','heaptest','heapinfo','fsinfo','echo usb-ring-monitor-ok','halt') }
    if ($BootWithKeyboard) { $commands=@('sdinfo','sdprobe','sdtest','sdscan','sdscan tail')+$commands[0..6]+@('usbports','usbidentify 1')+$commands[7..($commands.Count-1)] }
    if ($Hotplug) {
        $commands=@('usbports')+$commands[0..6]+@('usbports','usbkeytest 1','usbkeytest 2','qmp-mouse','usbidentify 2','qmp-remove','qmp-add','usbports',
            'usbkeytest 2','keyboard-timeout','usbkeytest 2','qmp-key-a','usbkeytest 2','qmp-key-b',
            'storagetest','qmp-remove','usbports')+$commands[7..($commands.Count-1)]
    }
    if ($WifiFiles) { $commands=@('usbdevices','storageinit','cryptotest','keytest','nettest','p256test','h2etest','saetest','rngtest','wififiles','wifiload','wifistart','wificontrol','wificaps','wifiprepare IN','wifiscan',"wifitarget Jack's","wifiauthprep Jack's","wifijoinprobe Jack's","wifisaeprobe Jack's","wifisaeauth Jack's","wifiassoc Jack's","wifidhcp Jack's","wifiping Jack's","wificonnect Jack's",'netinfo')+$commands[1..($commands.Count-1)] }
    $commands=@('mmuinit','exctest','mmutest','irqinit','timerstart','schedtest','taskinfo','timertest','irqinfo')+$commands[0..($commands.Count-2)]+@('schedtest','taskinfo','timertest','timerinfo','timerstop','exctest','halt')
    foreach ($line in $commands) {
        if ($process.HasExited) { throw $stderr.Result }
        if ($line -eq 'qmp-add') { Invoke-Qmp @{execute='device_add';arguments=@{driver='usb-kbd';bus='xhci.0';port='2';id='test-keyboard'}}; continue }
        if ($line -eq 'qmp-mouse') { Invoke-Qmp @{execute='device_add';arguments=@{driver='usb-mouse';bus='xhci.0';port='2';id='test-keyboard'}}; continue }
        if ($line -eq 'keyboard-timeout') { Start-Sleep -Seconds 9; continue }
        if ($line -eq 'qmp-key-a' -or $line -eq 'qmp-key-b') {
            Invoke-Qmp @{execute='send-key';arguments=@{keys=@(@{type='qcode';data=$line[-1].ToString()});'hold-time'=150}}
            Start-Sleep -Milliseconds 900
            continue
        }
        if ($line -eq 'qmp-remove') { Invoke-Qmp @{execute='device_del';arguments=@{id='test-keyboard'}} $true; continue }
        foreach ($character in ($line+"`r`n").ToCharArray()) {
            $process.StandardInput.Write($character); $process.StandardInput.Flush()
            Start-Sleep -Milliseconds 8
        }
        if ($line -eq 'wififiles') { Start-Sleep -Milliseconds 12000 }
        elseif ($line -eq 'keytest') { Start-Sleep -Seconds 2 }
        elseif ($line -eq 'saetest') { Start-Sleep -Seconds 20 }
        elseif ($line -eq 'p256test' -or $line -eq 'h2etest') { Start-Sleep -Seconds 12 }
        elseif ($line -eq 'storagetest') { Start-Sleep -Milliseconds 3000 } elseif ($line -eq 'timertest' -or $line -eq 'schedtest') { Start-Sleep -Milliseconds 1000 } else { Start-Sleep -Milliseconds 700 }
    }
    Start-Sleep -Milliseconds 500
    if (!$process.HasExited) { $process.Kill() }; $process.WaitForExit()
    $text=$stdout.Result
    $name=if ($Handoff) { 'usb-ring-el2' } else { 'usb-ring-el1' }
    if ($Enumerate) { $name=$name.Replace('ring','enum') }
    if ($Keyboard) { $name=$name.Replace('ring','keyboard') }
    if ($Storage) { $name=$name.Replace('ring','storage') }
    if ($Hotplug) { $name=$name.Replace('storage','hotplug') }
    if ($BootWithKeyboard) { $name=$name.Replace('storage','storage-keyboard') }
    if ($WifiFiles) { $name=$name.Replace('storage','wifi-files') }
    $text | Set-Content -Encoding ASCII (Join-Path $out "$name.log")
    $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
    if ([regex]::Matches($text,'PASS: three guarded kernel tasks yielded/slept/exited; callee registers/stack/heap data preserved; PMM/heap counts restored.').Count -ne 2 -or [regex]::Matches($text,'private_stack_timer_irqs=[1-9][0-9]*').Count -ne 2 -or $text.Contains('FAIL:')) { throw "$name tasks/USB regression; see $out/$name.log" }
    $fragments=@('QEMU qemu-xhci BAR0=0x10000000',
        'GICv2 initialization: ok timer_INTID=30','PASS: periodic timer IRQs delivered; asynchronous GPR/SP/NZCV sentinels preserved and foreground responsive.',
        'PASS: synchronous exception returned with all 31 GPRs, SP and NZCV preserved.','Periodic timer stopped; CPU IRQ masked.',
        'PASS: MMU null/guard translation faults, code/constants write faults, data execution fault; heap/PMM counts restored.',
        'PASS: four distinct pages written/read/freed; counters restored.',
        'Kernel heap: ready','PASS: heap alignment/zeroing/patterns/realloc/invalid free; live buffers and PMM pages restored.',
        'files=3 directories=13 data_pages=2','heap_buffers=3 capacity_bytes=544',"`r`nqemu_virt`r`n", "`r`nusb-ring-monitor-ok`r`n",'CPU halted.')
    if (!$Storage) { $fragments+=@('PASS: four xHCI No-op completions matched; controller halted.','completed=4','controller_changed=yes halted=yes','test already attempted; reboot before another attempt') }
    foreach ($fragment in $fragments) {
        if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
    }
    if ($text.Contains('FATAL')) { throw 'Unexpected exception in USB test' }
    if (!$text.Contains('USB inventory empty. Initialize storage or enumerate a device first.')) { throw "$name populated USB inventory before enumeration" }
    if ($text.Contains('FAIL:') -or $text.Contains('fault=yes') -or
        [regex]::Matches($text,'PASS: periodic timer IRQs delivered; asynchronous GPR/SP/NZCV sentinels preserved and foreground responsive.').Count -ne 2 -or
        [regex]::Matches($text,'PASS: synchronous exception returned with all 31 GPRs, SP and NZCV preserved.').Count -ne 2) { throw 'USB timer/exception regression failed' }
    if ($WifiFiles) {
        if (!$text.Contains('PASS: Ethernet/IPv4/UDP/DHCP public packet fixtures; no network I/O.')) { throw 'QEMU DHCP packet self-test failed' }
        if (!$text.Contains('PASS: DNS A query/compressed-answer public fixtures; no network I/O.')) { throw 'QEMU DNS packet self-test failed' }
        if (!$text.Contains('PASS: ARP/ICMP gateway public packet fixtures; no network I/O.') -or !$text.Contains('No Pi Wi-Fi radio on QEMU virt; gateway ping unavailable. No password requested.')) { throw 'QEMU ARP/ICMP fixtures or missing-radio refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; live session unavailable. No password requested.') -or !$text.Contains('Physical page allocator ready;')) { throw 'QEMU live-session refusal or external bitmap allocator failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; DHCP probe unavailable. No password requested.') -or !$text.Contains('No live Pi Wi-Fi DHCP lease on QEMU virt.')) { throw 'QEMU DHCP refusal/status failed' }
        if (!$text.Contains('PASS: AES-128, CMAC, key unwrap, SAE PTK, offline M2/M4, key-data and M3-retry known-answer tests.')) { throw 'QEMU WPA key self-test failed' }
        if (!$text.Contains('PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.') -or !$text.Contains('No Pi RNG200 source on QEMU virt; randomness unavailable.')) { throw 'QEMU crypto test / RNG refusal failed' }
        if (!$text.Contains('PASS: P-256 RFC5903 public/shared points, scalar boundaries and group identities.')) { throw 'QEMU P-256 self-test failed' }
        if (!$text.Contains('PASS: SAE H2E group-19 mapping, password token and MAC-bound password element.')) { throw 'QEMU H2E self-test failed' }
        if (!$text.Contains('PASS: SAE commit/confirm, key derivation, bad-confirm rejection and gated key export.')) { throw 'QEMU SAE exchange self-test failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; scanning unavailable.')) { throw 'QEMU scan refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; target check unavailable.')) { throw 'QEMU target preflight refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; firmware auth-policy check unavailable.')) { throw 'QEMU firmware auth-policy refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi capabilities on QEMU virt; query unavailable.') -or !$text.Contains('No Pi Wi-Fi regulatory target on QEMU virt; preparation unavailable.')) { throw 'QEMU capability/regulatory refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi control channel on QEMU virt; queries unavailable.')) { throw 'QEMU control-channel refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; external-SAE join probe unavailable.')) { throw 'QEMU join-probe refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; SAE commit probe unavailable. No password requested.')) { throw 'QEMU SAE-probe refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; SAE confirm test unavailable. No password requested.')) { throw 'QEMU SAE-confirm refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; association probe unavailable. No password requested.')) { throw 'QEMU association refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi firmware CPU on QEMU virt; startup unavailable.')) { throw 'QEMU startup refusal failed' }
        if (!$text.Contains('Wi-Fi firmware files: SHA256 pins verified') -or !$text.Contains('No Pi Wi-Fi firmware target on QEMU virt')) { throw 'Firmware binary file verification / QEMU refusal failed' }
        $pins=Get-Content -Raw (Join-Path $ProjectRoot 'boot/wifi/assets.json') | ConvertFrom-Json
        foreach ($pin in $pins.files | Where-Object { $_.name -ne 'WIFILIC.TXT' }) { if (!$text.Contains("$($pin.name) bytes=$($pin.bytes) SHA256=$($pin.sha256)")) { throw "Missing firmware hash: $($pin.name)" } }
    }
    if ($Enumerate -or $Storage) {
        if (!$text.Contains('PASS: USB device and configuration descriptors read.') -or !$text.Contains('USB mass storage BOT interface=')) { throw "USB enumeration failed; see $out/$name.log" }
        if ((Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash -ne $storageHash) { throw 'USB test disk image changed' }
    }
    if ($Keyboard) {
        if (!$text.Contains('PASS: USB device and configuration descriptors read.') -or !$text.Contains('state=descriptors-only') -or
            !$text.Contains('kind=hid-boot-keyboard') -or $text -notmatch '(?m)^  endpoint=0x[0-9a-f]+ direction=in type=interrupt max_packet=[1-9][0-9]* bInterval=[1-9]') {
            throw "USB keyboard inventory failed; see $out/$name.log"
        }
        Write-Output "PASS: $name boot keyboard and interrupt-in endpoint identified from cached descriptors"
        return
    }
    if ($Storage) {
        $bytes=New-Object byte[] (512*512)
        $stream=[IO.File]::OpenRead($storagePath)
        try { if ($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Test image too short' } } finally { $stream.Dispose() }
        [uint64]$hash=2166136261
        foreach ($b in $bytes) { $hash=(($hash -bxor [uint64]$b)*[uint64]16777619) -band [uint64]4294967295 }
        $expected='PASS: read-only storage sectors=512 fnv1a32=0x'+$hash.ToString('x16')
        foreach ($fragment in @('USB BOT: ready; boot read-only, data opt-in','device=usb-bot sector_size=512 boot=read-only data=opt-in','kernel=OUROS.IMG',$expected)) {
            if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
        }
        if ($text -notmatch 'ring_wraps_in=[1-9][0-9]* out=[1-9][0-9]* events=[1-9][0-9]*') { throw 'USB rings did not all wrap' }
        $inventories=[regex]::Matches($text,'(?m)^> usbdevices\r?\n(USB cached device [^\r\n]+ state=storage-ready\r?\n(?:USB [^\r\n]+\r?\n|  endpoint=[^\r\n]+\r?\n)+)')
        if ($inventories.Count -ne 2 -or $inventories[0].Value -cne $inventories[1].Value -or
            !$inventories[0].Value.Contains('driver=usb-bot') -or
            !$inventories[0].Value.Contains('direction=in type=bulk') -or !$inventories[0].Value.Contains('direction=out type=bulk')) {
            throw "USB storage inventory differs before and after reads; see $out/$name.log"
        }
        if ($text -match 'Storage attachment failed|FAT mount failed|Storage read test failed|fs:') { throw 'USB storage operation failed' }
        if ($BootWithKeyboard) {
            if ($text -notmatch 'USB root port=2 protocol=2 present=yes enabled=yes [^\r\n]+owner=usb-bot' -or
                !$text.Contains('kind=hid-boot-keyboard') -or !$text.Contains('PASS: USB accessory descriptors read.') -or
                !$text.Contains('microSD host status requires a Raspberry Pi 5.') -or
                !$text.Contains('microSD host probe requires a Raspberry Pi 5.') -or
                [regex]::Matches($text,'microSD read test requires a Raspberry Pi 5\.').Count -ne 3) {
                throw "$name failed storage selection or keyboard identification; see $out/$name.log"
            }
            Write-Output "PASS: $name skipped keyboard on port 1, mounted storage on port 2 and identified keyboard afterward"
        }
        if ($Hotplug) {
            $ports=[regex]::Matches($text,'(?m)^> usbports\r?\nUSB root ports controller=[^\r\n]+\r?\n((?:USB root port=[^\r\n]+\r?\n)+)')
            if (!$text.Contains('USB ports unavailable. Start USB storage first.') -or $ports.Count -ne 3 -or
                [regex]::Matches($text,[regex]::Escape($expected)).Count -ne 2) { throw "$name missing snapshots or storage reads" }
            if ([regex]::Matches($text,'USB keyboard test requires a connected spare USB2 port').Count -ne 2 -or
                !$text.Contains('kind=hid-boot-mouse') -or !$text.Contains('PASS: USB accessory descriptors read.') -or
                [regex]::Matches($text,'USB keyboard test ended: controller/event timeout').Count -ne 1 -or
                !$text.Contains('PASS: USB keyboard usage=0x0000000000000004 modifiers=0x0000000000000000; keyboard slot released.') -or
                !$text.Contains('PASS: USB keyboard usage=0x0000000000000005 modifiers=0x0000000000000000; keyboard slot released.')) {
                throw "$name keyboard input or safe port refusal failed; see $out/$name.log"
            }
            for ($i=0;$i -lt $ports.Count;++$i) {
                $presence=if ($i -eq 1) { 'yes' } else { 'no' }
                if ($ports[$i].Value -notmatch 'USB root port=1 protocol=2 present=yes enabled=yes [^\r\n]+owner=usb-bot' -or
                    $ports[$i].Value -notmatch "USB root port=2 protocol=2 present=$presence enabled=no [^\r\n]+owner=none") {
                    throw "$name incorrect storage or hotplug port state; see $out/$name.log"
                }
            }
            Write-Output "PASS: $name keyboard timeout, A/B reports, slot reuse and detach during storage use"
        }
        Write-Output "PASS: $name USB BOT inventory stable across FAT32 reads, host-matched sector hash, ring wraps and unchanged disk image"
    } else { Write-Output "PASS: $name real qemu-xhci DMA/completions, halt and repeat refusal; UART still responds" }
} finally {
    if ($qmp) { $qmp.Dispose() }
    if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
