[CmdletBinding()]
param([switch]$Handoff)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
$cases=if ($Handoff) { @(@('cortex-a76','off'),@('cortex-a76','on')) } else { @(@('cortex-a53','off'),@('cortex-a76','off')) }
foreach ($case in $cases) {
    $name="tasks-$($case[0])-$($case[1])";$start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe;$start.Arguments="-machine virt-9.2,virtualization=$($case[1]),secure=off -cpu $($case[0]) -accel tcg -smp 1 -m 128 -display none -monitor none -serial stdio -net none -kernel `"$kernel`""
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process;$process.StartInfo=$start;$started=$false
    try {
        if (!$process.Start()) { throw 'Cannot start QEMU' };$started=$true;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();Start-Sleep -Milliseconds 1000
        foreach ($line in @('schedtest','mmuinit','exctest','mmutest','irqinit','timerstart','schedtest','taskinfo','schedtest','taskinfo','timertest','heaptest','pagetest','mmutest','timerstop','schedtest','timerstart','schedtest','taskinfo','timerinfo','timerstop','excinfo','echo tasks-monitor-ok','fault')) {
            foreach ($c in ($line+"`r`n").ToCharArray()) { $process.StandardInput.Write($c);$process.StandardInput.Flush();Start-Sleep -Milliseconds 5 }
            Start-Sleep -Milliseconds $(if ($line -eq 'schedtest' -or $line -eq 'timertest') { 1200 } else { 250 })
        }
        Start-Sleep -Milliseconds 400;if (!$process.HasExited) { $process.Kill() };$process.WaitForExit();$text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$name.log");$stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
        foreach ($p in @('Task test refused: run mmuinit, irqinit and timerstart first.','last_result=passed','task=0 state=exited','task=1 state=exited','task=2 state=exited','sleeps=4 wakes=4 completed=8',
            'PASS: periodic timer IRQs delivered; asynchronous GPR/SP/NZCV sentinels preserved and foreground responsive.','tasks-monitor-ok','FATAL exception vector=4','ESR=0x00000000f2000042')) {
            if (!$text.Contains($p)) { throw "$name missing '$p'; see $out/$name.log" }
        }
        if ([regex]::Matches($text,'PASS: three guarded kernel tasks yielded/slept/exited; callee registers/stack/heap data preserved; PMM/heap counts restored.').Count -ne 3 -or [regex]::Matches($text,'private_stack_timer_irqs=[1-9][0-9]*').Count -ne 3 -or $text.Contains('FAIL:') -or $text.Contains('fault=yes') -or [regex]::Matches($text,'FATAL exception').Count -ne 1) { throw "$name task/context regression; see log" }
        Write-Output "PASS: ${name}: three real guarded stacks, round-robin yield, timed sleep/wakeup, implicit/explicit exit, registers/data, repeated runs and monitor/memory/IRQ/fatal-fault regression."
    } finally { if ($started -and !$process.HasExited) { $process.Kill();$process.WaitForExit() };$process.Dispose() }
}
