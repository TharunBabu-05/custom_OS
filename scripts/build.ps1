[CmdletBinding()]
param([ValidateSet('check','virt','rpi5','handoff-test')][string]$Target = 'check',
      [ValidateSet('debug','header')][string]$Console = 'debug')
. "$PSScriptRoot/windows-tools.ps1"
if ($Console -eq 'header' -and $Target -ne 'rpi5') { throw '-Console header requires -Target rpi5' }
Push-Location $ProjectRoot
try {
    $common = @('-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-Wstrict-prototypes',
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mgeneral-regs-only',
        '-mstrict-align','-mno-outline-atomics','-mabi=lp64','-mlittle-endian',
        '-fno-tree-loop-distribute-patterns','-Iinclude')
    function Verify-Elf([string]$Directory, [string]$Stem) {
        $elf = "$Directory/$Stem.elf"
        $headers = Invoke-Tool readelf @('-h','-l','-d',$elf)
        $headers | Set-Content -Encoding ASCII "$Directory/readelf.txt"
        $undefined = Invoke-Tool nm @('-u',$elf)
        Set-Content -Encoding ASCII "$Directory/undefined.txt" -Value ($undefined -join "`n")
        $headerText = $headers -join "`n"
        if ($headerText -notmatch 'Class:\s+ELF64' -or $headerText -notmatch 'Machine:\s+AArch64' -or
            $headerText -notmatch 'Type:\s+EXEC' -or $headerText -notmatch 'Data:.*little endian' -or
            $headerText -cmatch 'INTERP|NEEDED|DYNAMIC' -or $undefined) { throw "Invalid freestanding ELF: $elf" }
        Invoke-Tool objdump @('-d',$elf) | Set-Content -Encoding ASCII "$Directory/disassembly.txt"
        Invoke-Tool objcopy @('-O','binary',$elf,"$Directory/$Stem.bin")
        if ((Get-Item "$Directory/$Stem.bin").Length -eq 0) { throw 'Empty binary' }
    }
    if ($Target -eq 'check') {
        foreach ($board in @('rpi_zero2w','rpi5')) {
            $cpu = if ($board -eq 'rpi5') { 'cortex-a76' } else { 'cortex-a53' }
            $out = "build/toolchain/$board"
            New-Item -ItemType Directory -Force -Path $out | Out-Null
            ($common + "-mcpu=$cpu") -join ' ' | Set-Content -Encoding ASCII "$out/flags.txt"
            Invoke-Tool gcc ($common + @("-mcpu=$cpu",'-c','tests/toolchain/probe.c','-o',"$out/probe.o"))
            Invoke-Tool gcc @("-mcpu=$cpu",'-g','-c','tests/toolchain/probe.S','-o',"$out/probe-asm.o")
            Invoke-Tool ld @('--build-id=none','--no-undefined','-T','tests/toolchain/probe.ld',"-Map=$out/probe.map",'-o',"$out/probe.elf","$out/probe.o","$out/probe-asm.o")
            Verify-Elf $out 'probe'
            Write-Output "PASS: ${board}: freestanding ELF64 AArch64; no unresolved symbols or dynamic runtime."
        }
        Invoke-Tool gcc @('--version') | Set-Content -Encoding ASCII 'build/toolchain/compiler.txt'
        Invoke-Tool gdb @('--version') | Set-Content -Encoding ASCII 'build/toolchain/gdb.txt'
        Write-Output 'Compiler checks only: probe binaries are NOT bootable kernels.'
    } else {
        $boardSource='boards/qemu_virt/board.c'
        $entrySource='arch/aarch64/boot/start.S'
        $linker='linker/qemu_virt.ld'
        $cpu='cortex-a53'
        $linkOptions=@()
        $out='build/qemu_virt'
        if ($Target -ne 'virt') {
            $out="build/$Target"
            $entrySource='arch/aarch64/boot/firmware.S'
            $linker='linker/rpi5.ld'
            $cpu='cortex-a76'
            if ($Target -eq 'rpi5') { $boardSource='boards/rpi5/board.c'; $common+='-DOUROS_PI5=1' }
            else { $linkOptions=@('--defsym=BOOT_TEST_BASE=0x40080000') }
        }
        if ($Target -eq 'rpi5' -and $Console -eq 'header') {
            $out='build/rpi5-header'
            $common+='-DOUROS_RPI5_HEADER_UART=1'
        }
        New-Item -ItemType Directory -Force -Path $out | Out-Null
        $sources = @($entrySource,'arch/aarch64/interrupts/vectors.S',
            $boardSource,'drivers/uart/pl011.c','drivers/timer/arch_timer.c',
            'kernel/core/console.c','kernel/core/main.c','lib/memory.c',
            'kernel/memory/map.c','kernel/memory/fdt_memory.c','kernel/memory/pages.c',
            'kernel/memory/boot_memory.c','filesystem/ramfs.c','kernel/core/fs_commands.c',
            'drivers/storage/block.c','filesystem/fat32.c','filesystem/vfs.c','kernel/core/storage_commands.c',
            'kernel/core/dt_property.c','drivers/usb/xhci_observe.c','drivers/usb/pi5_usb_dt.c',
            'kernel/core/usb_commands.c')
        if ($Target -eq 'rpi5') { $sources+=@('drivers/storage/sdhci.c','drivers/storage/pi5_sd.c','drivers/storage/pi5_wifi_probe.c') }
        else { $sources+='drivers/storage/virtio_blk.c' }
        if ($Target -ne 'rpi5') { $sources+='kernel/core/fdt.c' }
        $objects = @()
        foreach ($source in $sources) {
            $object = "$out/" + ($source -replace '[/\\.]','_') + '.o'
            Invoke-Tool gcc ($common + @("-mcpu=$cpu",'-c',$source,'-o',$object))
            $objects += $object
        }
        Invoke-Tool ld ($linkOptions + @('--build-id=none','--no-undefined','-T',$linker,"-Map=$out/kernel.map",'-o',"$out/kernel.elf") + $objects)
        Verify-Elf $out 'kernel'
        if ($Target -ne 'virt') {
            $bytes=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot "$out/kernel.bin"))
            if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt32($bytes,56) -ne 0x644d5241 -or
                [BitConverter]::ToUInt64($bytes,8) -ne 0x80000 -or
                [BitConverter]::ToUInt64($bytes,16) -lt $bytes.Length -or
                [BitConverter]::ToUInt64($bytes,16) -ge 0x180000) { throw 'Invalid AArch64 Image header' }
        }
        if ($Target -eq 'rpi5') {
            Copy-Item -LiteralPath "$out/kernel.bin" -Destination "$out/ouros-rpi5.img"
            Write-Output "Built Pi 5 candidate: $out/ouros-rpi5.img. Console=$Console. Hardware boot remains UNTESTED."
        } else { Write-Output "Built $out/kernel.elf for QEMU only." }
    }
} finally { Pop-Location }
