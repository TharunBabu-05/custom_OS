[CmdletBinding()]
param([switch]$Handoff,[string]$StorageImage)
. "$PSScriptRoot/windows-tools.ps1"
$out=Join-Path $ProjectRoot $(if ($Handoff) { 'build/handoff-test' } else { 'build/qemu_virt' })
$kernel=Join-Path $out $(if ($Handoff) { 'kernel.bin' } else { 'kernel.elf' })
$storageOptions=''
if ($StorageImage) {
    $storagePath=(Resolve-Path -LiteralPath $StorageImage).Path
    $storageHash=(Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash
    $storageOptions=" -global virtio-mmio.force-legacy=false -drive file=`"$storagePath`",if=none,format=raw,readonly=on,id=bootdisk -device virtio-blk-device,drive=bootdisk"
}
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build the kernel first.' }
function Run-Case([string]$Name,[string]$Cpu,[int]$MemoryMB,[string[]]$Commands,[string[]]$Expected,[bool]$Virtualization=$false) {
    $start=New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName=$QemuExe
    $virt=if ($Virtualization) { 'on' } else { 'off' }
    $start.Arguments="-machine virt-9.2,virtualization=$virt,secure=off -cpu $Cpu -accel tcg -smp 1 -m $MemoryMB -display none -monitor none -serial stdio -net none -kernel `"$kernel`"$storageOptions"
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
        $mathSeen=@{}
        foreach ($line in $Commands) {
            # Pace UART input: a host pipe burst can overrun its 16-byte FIFO.
            foreach ($character in ($line+"`r`n").ToCharArray()) {
                $process.StandardInput.Write($character)
                $process.StandardInput.Flush()
                Start-Sleep -Milliseconds 5
            }
            if ($line -eq 'keytest') { Start-Sleep -Seconds 2 }
            elseif ($mathSeen.ContainsKey($line)) { Start-Sleep -Milliseconds 200 }
            elseif ($line -eq 'saetest') { Start-Sleep -Seconds 20;$mathSeen[$line]=$true }
            elseif ($line -eq 'p256test' -or $line -eq 'h2etest') { Start-Sleep -Seconds 12;$mathSeen[$line]=$true } else { Start-Sleep -Milliseconds 200 }
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
        if ($Name -notlike 'fault*') {
            if ([regex]::Matches($text,'Cached self-test result from this boot').Count -ne 3 -or
                [regex]::Matches($text,'Self-test elapsed_seconds=').Count -ne 3) { throw "$Name math self-test caching/timing failed" }
            $snapshots=[regex]::Matches($text,'managed_pages=(\d+) free_pages=(\d+) allocated_pages=(\d+)')
            if ($snapshots.Count -ne 4 -or $snapshots[0].Groups[3].Value -ne '3' -or
                $snapshots[1].Groups[3].Value -ne '5' -or $snapshots[2].Groups[3].Value -ne '3' -or
                $snapshots[3].Groups[3].Value -ne '3' -or
                $snapshots[0].Groups[2].Value -ne $snapshots[2].Groups[2].Value -or
                $snapshots[0].Groups[2].Value -ne $snapshots[3].Groups[2].Value -or
                ([long]$snapshots[0].Groups[2].Value-2) -ne [long]$snapshots[1].Groups[2].Value) {
                throw "$Name physical page counters did not balance (see $out/$Name.log)"
            }
            $filesystems=[regex]::Matches($text,'files=(\d+) directories=(\d+) data_pages=(\d+) data_bytes=(\d+)')
            if ($filesystems.Count -ne 2 -or $filesystems[0].Value -ne $filesystems[1].Value -or
                $filesystems[0].Groups[1].Value -ne '3' -or $filesystems[0].Groups[2].Value -ne '12' -or
                $filesystems[0].Groups[3].Value -ne '3') { throw "$Name filesystem counters did not balance" }
        }
        Write-Output "PASS: $Name"
    } finally {
        if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
$commands=@('help','info','cpuinfo','cryptotest','keytest','nettest','p256test','h2etest','saetest','p256test','h2etest','saetest','rngtest','meminfo','memmap','pagetest','pagealloc','pagealloc','meminfo',
    'pagefree 0','pagefree 0','pagefree 1','pagefree 999','meminfo',
    'fsinfo','pwd','ls /','cat /etc/board','cat /README.txt','cd /tmp','mkdir work','cd work','pwd',
    'touch empty.txt','write note.txt filesystem-ok','cat ./note.txt','ls .','cd ..','rmdir work',
    'rm work/note.txt','rm work/empty.txt','rmdir work','cd /','cat /tmp/work/note.txt','usbinfo','usbprobe',
    'cd /etc/hostname','rmdir /','rm /tmp','fsinfo','meminfo','echo native-windows-ok',
    "echo bacX`bkspace",'uptime','bogus',('x'*140),'echo recovered','halt')
$expected=@('OurOS: AArch64 kernel booted','clock_hz=24000000','board=qemu_virt | EL1',
    'PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.',
    'PASS: AES-128, CMAC, key unwrap, SAE PTK, offline M2/M4, key-data and M3-retry known-answer tests.',
    'PASS: Ethernet/IPv4/UDP/DHCP public packet fixtures; no network I/O.',
    'PASS: ARP/ICMP gateway public packet fixtures; no network I/O.',
    'PASS: P-256 RFC5903 public/shared points, scalar boundaries and group identities.',
    'PASS: SAE H2E group-19 mapping, password token and MAC-bound password element.',
    'PASS: SAE commit/confirm, key derivation, bad-confirm rejection and gated key export.',
    "Cached self-test result from this boot; append ' force' to rerun.",
    'No Pi RNG200 source on QEMU virt; randomness unavailable.',
    'help info cpuinfo meminfo uptime','timer_hz=',"`r`nnative-windows-ok`r`n", "`r`nbackspace`r`n",'uptime_seconds=',
    'Unknown command. Type help.','Input too long; command discarded.',"`r`nrecovered`r`n",'CPU halted.',
    'Memory: ready','reason=kernel/BSS/stack','reason=DTB','reason=physical page bitmap','Bitmap arena=',
    'PASS: four distinct pages written/read/freed; counters restored.',
    'Allocated slot=0','Allocated slot=1','Freed slot=0','Freed slot=1','Invalid or unallocated page slot.',
    'Root filesystem ready: RAMFS at / (volatile).','rootfs=ramfs (volatile; files disappear after reboot)',
    '/tmp/work',"`r`nfilesystem-ok`r`n",'Welcome to OurOS. This root filesystem lives in RAM.',
    'fs: directory not empty','fs: path not found','fs: not a directory',
    'fs: root or current directory is busy','fs: is a directory',
    'USB: discovery diagnostics; storageinit enables experimental read-only USB2 BOT.',
    'No Pi RP1 USB controller on QEMU virt; no MMIO attempted.')
if ($StorageImage) {
    $commands=$commands[0..($commands.Count-2)]+@('storageinit','storageinfo','ls /boot','cd /boot',
        'pwd','cat CONFIG.TXT','cat SOURCE.TXT','write CONFIG.TXT blocked','touch new.txt',
        'mkdir newdir','rm CONFIG.TXT','cd /boot/CONFIG.TXT/..','cd ..','pwd','wifi','wifiinfo','wifiprobe','wifiinit','wificores','wifistart','wificontrol','wificaps','wifiprepare IN','wifiscan',"wifitarget Jack's","wifiauthprep Jack's","wifijoinprobe Jack's","wifisaeprobe Jack's","wifisaeauth Jack's","wifiassoc Jack's","wifidhcp Jack's","wifiping Jack's","wificonnect Jack's",'netinfo','halt')
    $expected+=@('/boot: FAT32 read-only','device=qemu-virtio-blk','CONFIG.TXT','kernel=OUROS.IMG',
        'Official DTB and notices:','fs: read-only filesystem','Wi-Fi state: manual firmware/control bring-up',
        'Requested network security: WPA3-Personal','No Pi Wi-Fi SDIO device on QEMU virt.',
        'No Pi Wi-Fi SDIO device on QEMU virt; WPA3 association unavailable.',
        'No Pi Wi-Fi cores on QEMU virt; use the Pi header build.',
        'No Pi Wi-Fi firmware CPU on QEMU virt; startup unavailable.',
        'No Pi Wi-Fi control channel on QEMU virt; queries unavailable.',
        'No Pi Wi-Fi capabilities on QEMU virt; query unavailable.',
        'No Pi Wi-Fi regulatory target on QEMU virt; preparation unavailable.',
        'No Pi Wi-Fi radio on QEMU virt; scanning unavailable.',
        'No Pi Wi-Fi radio on QEMU virt; target check unavailable.',
        'No Pi Wi-Fi radio on QEMU virt; firmware auth-policy check unavailable.',
        'No Pi Wi-Fi radio on QEMU virt; external-SAE join probe unavailable.',
        'No Pi Wi-Fi radio on QEMU virt; SAE commit probe unavailable. No password requested.',
        'No Pi Wi-Fi radio on QEMU virt; SAE confirm test unavailable. No password requested.',
        'No Pi Wi-Fi radio on QEMU virt; association probe unavailable. No password requested.',
        'No Pi Wi-Fi radio on QEMU virt; DHCP probe unavailable. No password requested.',
        'No Pi Wi-Fi radio on QEMU virt; gateway ping unavailable. No password requested.',
        'No Pi Wi-Fi radio on QEMU virt; live session unavailable. No password requested.',
        'No live Pi Wi-Fi DHCP lease on QEMU virt.')
}
if ($Handoff) {
    Run-Case 'handoff-el1' 'cortex-a76' 128 $commands ($expected+@('entered EL1 running EL1','Reported RAM: 134.217 MB (128.000 MiB)'))
    Run-Case 'handoff-el2' 'cortex-a76' 128 $commands ($expected+@('entered EL2 running EL1','Reported RAM: 134.217 MB (128.000 MiB)')) $true
    Run-Case 'fault-el2' 'cortex-a76' 128 @('fault') @('entered EL2 running EL1','FATAL exception vector=4','ESR=0x00000000f2000042') $true
} else {
    Run-Case 'monitor-a53' 'cortex-a53' 128 $commands ($expected+@('bytes=134217728','MIDR=0x00000000410fd034','Reported RAM: 134.217 MB (128.000 MiB)'))
    Run-Case 'monitor-a76' 'cortex-a76' 512 $commands ($expected+@('bytes=536870912','MIDR=0x00000000414fd0b1','Reported RAM: 536.870 MB (512.000 MiB)'))
    Run-Case 'fault-a53' 'cortex-a53' 128 @('fault') @('FATAL exception vector=4','ESR=0x00000000f2000042','CPU halted.')
}
if ($StorageImage -and (Get-FileHash -LiteralPath $storagePath -Algorithm SHA256).Hash -ne $storageHash) {
    throw 'Storage image changed during read-only QEMU tests'
}
if ($StorageImage) { Write-Output 'PASS: read-only test image hash unchanged' }
