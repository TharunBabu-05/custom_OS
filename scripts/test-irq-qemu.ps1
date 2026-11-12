[CmdletBinding()]
param([switch]$Handoff)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build selected kernel first' }
$cases=if ($Handoff) { @(@('cortex-a76','off'),@('cortex-a76','on')) } else { @(@('cortex-a53','off'),@('cortex-a76','off')) }
foreach ($case in $cases) {
    $name="irq-$($case[0])-$($case[1])"
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe;$start.Arguments="-machine virt-9.2,virtualization=$($case[1]),secure=off -cpu $($case[0]) -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -kernel `"$kernel`""
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process;$process.StartInfo=$start;$started=$false
    try {
        if (!$process.Start()) { throw 'Cannot start QEMU' };$started=$true;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 900
        foreach ($line in @('exctest','timerstart','irqinit','timerstart','mmuinit','timerstop','mmuinit','mmutest','irqinit','timerstart','timerinfo','timertest','exctest','heaptest','pagetest','mmutest','write /tmp/irq.txt irq-files-ok','cat /tmp/irq.txt','rm /tmp/irq.txt','timerinfo','timerstop','timerinfo','uptime','timerinfo','timerstart','timertest','timerstop','exctest','excinfo','echo irq-monitor-ok','fault')) {
            foreach ($c in ($line+"`r`n").ToCharArray()) { $process.StandardInput.Write($c);$process.StandardInput.Flush();Start-Sleep -Milliseconds 5 }
            Start-Sleep -Milliseconds $(if ($line -eq 'timertest') { 800 } else { 220 })
        }
        Start-Sleep -Milliseconds 400;if (!$process.HasExited) { $process.Kill() };$process.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$name.log");$stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
        foreach ($fragment in @('GICv2 initialization: ok timer_INTID=30','Periodic physical timer started: requested_hz=100','Timer: running CPU_IRQ=unmasked','Timer: stopped CPU_IRQ=masked','Timer start refused: run irqinit',
          'MMU activation deferred: timerstop first, then mmuinit. No activation attempt consumed.',
          'PASS: MMU null/guard translation faults, code/constants write faults, data execution fault; heap/PMM counts restored.',
          'PASS: heap alignment/zeroing/patterns/realloc/invalid free; live buffers and PMM pages restored.',
          'PASS: four distinct pages written/read/freed; counters restored.','irq-files-ok','irq-monitor-ok','FATAL exception vector=4','ESR=0x00000000f2000042','Exception class: AArch64 BRK')) {
            if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
        }
        if ([regex]::Matches($text,'PASS: periodic timer IRQs delivered').Count -ne 2 -or [regex]::Matches($text,'PASS: synchronous exception returned').Count -ne 3 -or $text.Contains('FAIL:') -or $text.Contains('fault=yes')) { throw "$name timer/context regression; see log" }
        $ticks=[regex]::Matches($text,'delivered=(\d+)');if (!$ticks.Count -or [long]$ticks[1].Groups[1].Value -lt 20) { throw "$name insufficient timer progress" }
        $stopped=[regex]::Matches($text,'(?s)Timer: stopped CPU_IRQ=masked.*?delivered=(\d+)');if ($stopped.Count -ne 2 -or $stopped[0].Groups[1].Value -ne $stopped[1].Groups[1].Value) { throw "$name timer progressed after stop" }
        Write-Output "PASS: ${name}: real timer IRQs, stop/restart, full synchronous register return, MMU/heap/PMM/files and unexpected BRK remains fatal."
    } finally { if ($started -and !$process.HasExited) { $process.Kill();$process.WaitForExit() };$process.Dispose() }
}
