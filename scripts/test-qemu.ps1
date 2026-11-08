[CmdletBinding()]
param([switch]$Handoff)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build the kernel first.' }
function Run-Case([string]$Name,[string]$Cpu,[int]$MemoryMB,[string[]]$Commands,[string[]]$Expected,[bool]$Virtualization=$false) {
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $virt=if ($Virtualization) { 'on' } else { 'off' }
    $start.Arguments="-machine virt-9.2,virtualization=$virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m $MemoryMB -display none -monitor none -serial stdio -net none -kernel `"$kernel`""
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process
    $process.StartInfo=$start
    $started=$false
    try {
        if (!$process.Start()) { throw 'Cannot start QEMU' }
        $started=$true
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        Start-Sleep -Milliseconds 800
        if ($process.HasExited) { throw $stderr.Result }
        foreach ($line in $Commands) {
            # Pace UART input: a host pipe burst can overrun its 16-byte FIFO.
            foreach ($character in ($line+"`r`n").ToCharArray()) {
                $process.StandardInput.Write($character)
                $process.StandardInput.Flush()
                Start-Sleep -Milliseconds 5
            }
            Start-Sleep -Milliseconds 200
        }
        Start-Sleep -Milliseconds 800
        if ($process.HasExited) { throw "QEMU terminated unexpectedly: $($stderr.Result)" }
        # The monitor/fault handler deliberately remains running. Stop only our child.
        $process.Kill()
        $process.WaitForExit()
        $text=$stdout.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out "$Name.log")
        $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
        foreach ($fragment in $Expected) {
            if (!$text.Contains($fragment)) { throw "$Name missing expected output: $fragment (see $out/$Name.log)" }
        }
        if ($Name -notlike 'fault*' -and $text.Contains('FATAL')) { throw 'Unexpected exception' }
        Write-Output "PASS: $Name"
    } finally {
        if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
$commands=@('help','info','cpuinfo','meminfo','echo native-windows-ok',"echo bacX`bkspace",'uptime','bogus',('x'*140),'echo recovered','halt')
$expected=@('OurOS: AArch64 kernel booted','clock_hz=24000000','board=qemu_virt | EL1',
    'help info cpuinfo meminfo uptime','timer_hz=',"`r`nnative-windows-ok`r`n", "`r`nbackspace`r`n",'uptime_seconds=',
    'Unknown command. Type help.','Input too long; command discarded.',"`r`nrecovered`r`n",'CPU halted.')
if ($Handoff) {
    Run-Case 'handoff-el1' 'cortex-a76' 128 $commands ($expected+@('entered EL1 running EL1'))
    Run-Case 'handoff-el2' 'cortex-a76' 128 $commands ($expected+@('entered EL2 running EL1')) $true
    Run-Case 'fault-el2' 'cortex-a76' 128 @('fault') @('entered EL2 running EL1','FATAL exception vector=4','ESR=0x00000000f2000042') $true
} else {
    Run-Case 'monitor-a53' 'cortex-a53' 128 $commands ($expected+@('bytes=134217728','MIDR=0x00000000410fd034'))
    Run-Case 'monitor-a76' 'cortex-a76' 512 $commands ($expected+@('bytes=536870912','MIDR=0x00000000414fd0b1'))
    Run-Case 'fault-a53' 'cortex-a53' 128 @('fault') @('FATAL exception vector=4','ESR=0x00000000f2000042','CPU halted.')
}
