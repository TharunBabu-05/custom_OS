[CmdletBinding()]
param()
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot 'build/qemu_virt'
$kernel=Join-Path $out 'kernel.elf'
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build the kernel first.' }
$start=New-Object System.Diagnostics.ProcessStartInfo
$start.FileName=$QemuExe
$start.Arguments="-machine virt-9.2,virtualization=off,secure=off -cpu cortex-a53 -accel tcg -smp 1 -m 128 -display none -monitor none -serial null -net none -kernel `"$kernel`" -S -gdb tcp:127.0.0.1:12349"
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardError=$true
$process=New-Object System.Diagnostics.Process
$process.StartInfo=$start
$started=$false
try {
    if (!$process.Start()) { throw 'Cannot start QEMU' }
    $started=$true
    $stderr=$process.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 500
    if ($process.HasExited) { throw $stderr.Result }
    $commands=Join-Path $out 'test.gdb'
    @'
set pagination off
set confirm off
set remotetimeout 5
set tcp connect-timeout 5
target remote 127.0.0.1:12349
set variable console_ready=1
break kernel_main
continue
printf "BOOT_STACK_ALIGNMENT=%lu\n", ((unsigned long)$sp & 15)
printf "BSS_CONSOLE_READY=%d\n", console_ready
printf "ENTRY_REACHED_KERNEL_MAIN\n"
detach
quit
'@ | Set-Content -Encoding ASCII $commands
    # Limit the debugger itself, so a broken boot cannot hang automation.
    $gdbStart=New-Object System.Diagnostics.ProcessStartInfo
    $gdbStart.FileName=Join-Path $ToolBin 'aarch64-none-elf-gdb.exe'
    $gdbStart.Arguments="-batch -x `"$commands`" `"$kernel`""
    $gdbStart.UseShellExecute=$false
    $gdbStart.CreateNoWindow=$true
    $gdbStart.RedirectStandardOutput=$true
    $gdbStart.RedirectStandardError=$true
    $gdb=New-Object System.Diagnostics.Process
    $gdb.StartInfo=$gdbStart
    $gdbStarted=$false
    try {
        if (!$gdb.Start()) { throw 'Cannot start GDB' }
        $gdbStarted=$true
        $stdoutTask=$gdb.StandardOutput.ReadToEndAsync()
        $errorTask=$gdb.StandardError.ReadToEndAsync()
        if (!$gdb.WaitForExit(15000)) { throw 'GDB test timed out' }
        $text=$stdoutTask.Result
        $text | Set-Content -Encoding ASCII (Join-Path $out 'gdb.log')
        $errorTask.Result | Set-Content -Encoding ASCII (Join-Path $out 'gdb.stderr.log')
        if ($gdb.ExitCode -ne 0 -or !$text.Contains('ENTRY_REACHED_KERNEL_MAIN') -or
            !$text.Contains('BOOT_STACK_ALIGNMENT=0') -or !$text.Contains('BSS_CONSOLE_READY=0')) {
            throw 'GDB boot-state verification failed; see build/qemu_virt/gdb*.log'
        }
        Write-Output 'PASS: native GDB reached kernel_main; stack aligned; BSS console flag zero'
    } finally {
        if ($gdbStarted -and !$gdb.HasExited) { $gdb.Kill(); $gdb.WaitForExit() }
        $gdb.Dispose()
    }
} finally {
    if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
