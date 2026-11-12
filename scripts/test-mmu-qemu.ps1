[CmdletBinding()]
param([switch]$Handoff)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build the selected kernel first.' }
$cases=if ($Handoff) { @(@('cortex-a76',128,'off'),@('cortex-a76',128,'on')) } else { @(@('cortex-a53',128,'off'),@('cortex-a76',4096,'off')) }
foreach ($case in $cases) {
    $name="mmu-$($case[0])-$($case[1])-$($case[2])"
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $start.Arguments="-machine virt-9.2,virtualization=$($case[2]),secure=off -cpu $($case[0]) -accel tcg -smp 1 -m $($case[1]) -display none -monitor none -serial stdio -net none -kernel `"$kernel`""
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process; $process.StartInfo=$start; $started=$false
    try {
        if (!$process.Start()) { throw 'Cannot start QEMU' }; $started=$true
        $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
        Start-Sleep -Milliseconds 1200
        foreach ($command in @('mmuinfo','mmutest','mmuinit','mmuinit','mmutest','mmuinfo','heaptest','pagetest','fsinfo','echo mmu-monitor-ok','mmutest','mmuinfo','uptime','fault')) {
            if ($process.HasExited) { throw $stderr.Result }
            foreach ($c in ($command+"`r`n").ToCharArray()) { $process.StandardInput.Write($c);$process.StandardInput.Flush();Start-Sleep -Milliseconds 5 }
            Start-Sleep -Milliseconds 350
        }
        Start-Sleep -Milliseconds 400
        if (!$process.HasExited) { $process.Kill() };$process.WaitForExit()
        $text=$stdout.Result;$text | Set-Content -Encoding ASCII (Join-Path $out "$name.log")
        $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$name.stderr.log")
        foreach ($fragment in @('MMU: off (run mmuinit)','MMU test unavailable: run mmuinit successfully first.',
            'MMU enabled: kernel code RO executable;','MMU activation already attempted or MMU already on;',
            'MMU: on; verified identity profile','last_pass_mask=0x000000000000001f',
            'PASS: heap alignment/zeroing/patterns/realloc/invalid free; live buffers and PMM pages restored.',
            'PASS: four distinct pages written/read/freed; counters restored.','heap_buffers=3 capacity_bytes=544',
            'mmu-monitor-ok','FATAL exception vector=4','ESR=0x00000000f2000042')) {
            if (!$text.Contains($fragment)) { throw "$name missing '$fragment'; see $out/$name.log" }
        }
        if ([regex]::Matches($text,'PASS: MMU null/guard translation faults, code/constants write faults, data execution fault; heap/PMM counts restored.').Count -ne 2 -or
            [regex]::Matches($text,' caught\r?\n').Count -ne 10 -or [regex]::Matches($text,'FATAL exception').Count -ne 1) { throw "$name fault recovery/repeated protection tests failed; see log" }
        Write-Output "PASS: ${name}: MMU control readback, five real protection faults twice, heap/PMM/RAMFS and fatal BRK after recovery."
    } finally {
        if ($started -and !$process.HasExited) { $process.Kill();$process.WaitForExit() };$process.Dispose()
    }
}
