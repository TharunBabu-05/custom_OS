[CmdletBinding()]
param([switch]$Handoff,[switch]$Enumerate,[switch]$Storage,[switch]$WifiFiles,[string]$StorageImage='build/rpi5-header/OurOS-Pi5-header-experimental-sd.img')
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build virt or handoff-test first.' }
$start=New-Object System.Diagnostics.ProcessStartInfo
$start.FileName=$QemuExe
$virt=if ($Handoff) { 'on' } else { 'off' }
$storageOptions=''
if ($Enumerate -and $Storage) { throw 'Choose -Enumerate or -Storage' }
if ($WifiFiles -and !$Storage) { throw '-WifiFiles requires -Storage' }
if ($Enumerate -or $Storage) {
    $storagePath=(Resolve-Path -LiteralPath $StorageImage).Path
    $storageHash=(Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash
    $storageOptions=" -drive file=`"$storagePath`",if=none,format=raw,readonly=on,id=usbdisk -device usb-storage,bus=xhci.0,drive=usbdisk"
}
$xhciDevice=if ($Enumerate -or $Storage) { 'qemu-xhci,id=xhci,p3=0' } else { 'qemu-xhci,id=xhci' }
$start.Arguments="-machine virt-9.2,virtualization=$virt,secure=off -cpu cortex-a76 -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device $xhciDevice -kernel `"$kernel`"$storageOptions"
$start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$process=New-Object System.Diagnostics.Process; $process.StartInfo=$start
$started=$false
try {
    if (!$process.Start()) { throw 'Cannot start QEMU' }; $started=$true
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 1000
    $usbCommand=if ($Enumerate) { 'usbenum' } else { 'usbtest' }
    $commands=@($usbCommand,$usbCommand,'uptime','cat /etc/board','pagetest','fsinfo','echo usb-ring-monitor-ok','halt')
    if ($Storage) { $commands=@('storageinit','storageinit','storageinfo','ls /boot','cat /boot/CONFIG.TXT','storagetest','storageinfo','cat /boot/SOURCE.TXT','uptime','cat /etc/board','pagetest','fsinfo','echo usb-ring-monitor-ok','halt') }
    if ($WifiFiles) { $commands=@('storageinit','cryptotest','rngtest','wififiles','wifiload','wifistart','wificontrol','wificaps','wifiprepare IN','wifiscan')+$commands[1..($commands.Count-1)] }
    foreach ($line in $commands) {
        if ($process.HasExited) { throw $stderr.Result }
        foreach ($character in ($line+"`r`n").ToCharArray()) {
            $process.StandardInput.Write($character); $process.StandardInput.Flush()
            Start-Sleep -Milliseconds 8
        }
        if ($line -eq 'wififiles') { Start-Sleep -Milliseconds 12000 }
        elseif ($line -eq 'storagetest') { Start-Sleep -Milliseconds 3000 } else { Start-Sleep -Milliseconds 700 }
    }
    Start-Sleep -Milliseconds 500
    if (!$process.HasExited) { $process.Kill() }; $process.WaitForExit()
    $text=$stdout.Result
    $name=if ($Handoff) { 'usb-ring-el2' } else { 'usb-ring-el1' }
    if ($Enumerate) { $name=$name.Replace('ring','enum') }
    if ($Storage) { $name=$name.Replace('ring','storage') }
    if ($WifiFiles) { $name=$name.Replace('storage','wifi-files') }
    $text | Set-Content -Encoding ASCII (Join-Path $out "$name.log")
    $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
    $fragments=@('QEMU qemu-xhci BAR0=0x10000000',
        'PASS: four distinct pages written/read/freed; counters restored.',
        'files=3 directories=12 data_pages=3',"`r`nqemu_virt`r`n", "`r`nusb-ring-monitor-ok`r`n",'CPU halted.')
    if (!$Storage) { $fragments+=@('PASS: four xHCI No-op completions matched; controller halted.','completed=4','controller_changed=yes halted=yes','test already attempted; reboot before another attempt') }
    foreach ($fragment in $fragments) {
        if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
    }
    if ($text.Contains('FATAL')) { throw 'Unexpected exception in USB test' }
    if ($WifiFiles) {
        if (!$text.Contains('PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.') -or !$text.Contains('No Pi RNG200 source on QEMU virt; randomness unavailable.')) { throw 'QEMU crypto test / RNG refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi radio on QEMU virt; scanning unavailable.')) { throw 'QEMU scan refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi capabilities on QEMU virt; query unavailable.') -or !$text.Contains('No Pi Wi-Fi regulatory target on QEMU virt; preparation unavailable.')) { throw 'QEMU capability/regulatory refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi control channel on QEMU virt; queries unavailable.')) { throw 'QEMU control-channel refusal failed' }
        if (!$text.Contains('No Pi Wi-Fi firmware CPU on QEMU virt; startup unavailable.')) { throw 'QEMU startup refusal failed' }
        if (!$text.Contains('Wi-Fi firmware files: SHA256 pins verified') -or !$text.Contains('No Pi Wi-Fi firmware target on QEMU virt')) { throw 'Firmware binary file verification / QEMU refusal failed' }
        $pins=Get-Content -Raw (Join-Path $ProjectRoot 'boot/wifi/assets.json') | ConvertFrom-Json
        foreach ($pin in $pins.files | Where-Object { $_.name -ne 'WIFILIC.TXT' }) { if (!$text.Contains("$($pin.name) bytes=$($pin.bytes) SHA256=$($pin.sha256)")) { throw "Missing firmware hash: $($pin.name)" } }
    }
    if ($Enumerate -or $Storage) {
        if (!$text.Contains('PASS: USB device and configuration descriptors read.') -or !$text.Contains('USB mass storage BOT interface=')) { throw "USB enumeration failed; see $out/$name.log" }
        if ((Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash -ne $storageHash) { throw 'USB test disk image changed' }
    }
    if ($Storage) {
        $bytes=New-Object byte[] (512*512)
        $stream=[IO.File]::OpenRead($storagePath)
        try { if ($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length) { throw 'Test image too short' } } finally { $stream.Dispose() }
        [uint64]$hash=2166136261
        foreach ($b in $bytes) { $hash=(($hash -bxor [uint64]$b)*[uint64]16777619) -band [uint64]4294967295 }
        $expected='PASS: read-only storage sectors=512 fnv1a32=0x'+$hash.ToString('x16')
        foreach ($fragment in @('USB BOT: ready, read-only','device=usb-bot sector_size=512 access=read-only','kernel=OUROS.IMG',$expected)) {
            if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
        }
        if ($text -notmatch 'ring_wraps_in=[1-9][0-9]* out=[1-9][0-9]* events=[1-9][0-9]*') { throw 'USB rings did not all wrap' }
        if ($text -match 'Storage attachment failed|FAT mount failed|Storage read test failed|fs:') { throw 'USB storage operation failed' }
        Write-Output "PASS: $name USB BOT capacity, FAT32 file reads, 512-sector host-matched hash, all ring wraps, repeated mount and unchanged disk image"
    } else { Write-Output "PASS: $name real qemu-xhci DMA/completions, halt and repeat refusal; UART still responds" }
} finally {
    if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
