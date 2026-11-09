[CmdletBinding()]
param([switch]$Handoff)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build virt or handoff-test first.' }
$start=New-Object System.Diagnostics.ProcessStartInfo
$start.FileName=$QemuExe
$virt=if ($Handoff) { 'on' } else { 'off' }
$start.Arguments="-machine virt-9.2,virtualization=$virt,secure=off -cpu cortex-a76 -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -device qemu-xhci -kernel `"$kernel`""
$start.UseShellExecute=$false; $start.CreateNoWindow=$true
$start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
$process=New-Object System.Diagnostics.Process; $process.StartInfo=$start
$started=$false
try {
    if (!$process.Start()) { throw 'Cannot start QEMU' }; $started=$true
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 1000
    foreach ($line in @('usbtest','usbtest','uptime','cat /etc/board','pagetest','fsinfo','echo usb-ring-monitor-ok','halt')) {
        if ($process.HasExited) { throw $stderr.Result }
        foreach ($character in ($line+"`r`n").ToCharArray()) {
            $process.StandardInput.Write($character); $process.StandardInput.Flush()
            Start-Sleep -Milliseconds 8
        }
        Start-Sleep -Milliseconds 700
    }
    Start-Sleep -Milliseconds 500
    if (!$process.HasExited) { $process.Kill() }; $process.WaitForExit()
    $text=$stdout.Result
    $name=if ($Handoff) { 'usb-ring-el2' } else { 'usb-ring-el1' }
    $text | Set-Content -Encoding ASCII (Join-Path $out "$name.log")
    $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
    foreach ($fragment in @('QEMU qemu-xhci BAR0=0x10000000','PASS: four xHCI No-op completions matched; controller halted.',
        'completed=4','controller_changed=yes halted=yes','test already attempted; reboot before another attempt',
        'PASS: four distinct pages written/read/freed; counters restored.',
        'files=3 directories=12 data_pages=3',"`r`nqemu_virt`r`n", "`r`nusb-ring-monitor-ok`r`n",'CPU halted.')) {
        if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
    }
    if ($text.Contains('FATAL')) { throw 'Unexpected exception in USB test' }
    Write-Output "PASS: $name real qemu-xhci DMA/completions, halt and repeat refusal; UART still responds"
} finally {
    if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
