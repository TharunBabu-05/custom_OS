[CmdletBinding()]
param([ValidateSet('check','virt','rpi5','handoff-test')][string]$Target = 'check',
      [ValidateSet('debug','header')][string]$Console = 'debug',[switch]$KeepBuildFiles)
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
        $common+=@('-ffunction-sections','-fdata-sections')
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
            if ($Target -eq 'rpi5') {
                $boardSource='boards/rpi5/board.c'
                # Avoid optional instruction padding before the firmware's fixed DTB address.
                $common+=@('-DOUROS_PI5=1','-fno-align-functions','-fno-align-jumps','-fno-align-labels','-fno-align-loops')
            }
            else { $linkOptions=@('--defsym=BOOT_TEST_BASE=0x40080000') }
        }
        if ($Target -eq 'rpi5' -and $Console -eq 'header') {
            $out='build/rpi5-header'
            $common+='-DOUROS_RPI5_HEADER_UART=1'
        }
        New-Item -ItemType Directory -Force -Path $out | Out-Null
        $sources = @($entrySource,'arch/aarch64/interrupts/vectors.S','arch/aarch64/interrupts/test_context.S','arch/aarch64/interrupts/test_irq_context.S','arch/aarch64/interrupts/irq.c','kernel/interrupts/exception.c','kernel/interrupts/tick.c','kernel/interrupts/irq_work.c','drivers/irq/gic.c','drivers/irq/gic_dt.c','drivers/irq/irq_probe.c','arch/aarch64/mmu/control.S','arch/aarch64/mmu/runtime.c','kernel/memory/mmu_tables.c',
            $boardSource,'drivers/uart/pl011.c','drivers/timer/arch_timer.c','arch/aarch64/context/switch.S','arch/aarch64/context/test_switch.S','kernel/scheduler/policy.c','kernel/scheduler/tasks.c',
            'kernel/core/console.c','kernel/core/terminal.c','kernel/core/console_input.c','kernel/core/main.c','kernel/core/trust_time_commands.c','kernel/core/reboot.c','kernel/scheduler/network_worker.c','arch/aarch64/boot/reset.c','drivers/reset/reset_dt.c','lib/memory.c',
            'arch/aarch64/boot/cpu_commands.c','kernel/core/cpu_topology.c',
            'kernel/memory/map.c','kernel/memory/fdt_memory.c','kernel/memory/pages.c',
            'kernel/memory/boot_memory.c','kernel/memory/heap.c','kernel/memory/heap_commands.c','filesystem/ramfs.c','kernel/core/fs_commands.c',
            'drivers/storage/block.c','filesystem/fat32.c','filesystem/persist.c','filesystem/vfs.c','filesystem/kfile.c','kernel/core/file_commands.c','kernel/core/storage_commands.c',
            'kernel/process/app_args.c','kernel/process/app_env.c','kernel/process/app_profile.c','kernel/process/elf_app.c','kernel/process/user_mmu.c','kernel/process/user_heap.c','kernel/process/byte_pipe.c','kernel/process/process.c','kernel/core/process_commands.c','kernel/core/app_files.c','arch/aarch64/context/user.S',
            'kernel/core/dt_property.c','drivers/usb/xhci_observe.c','drivers/usb/pi5_usb_dt.c',
            'kernel/core/usb_commands.c','drivers/usb/xhci_test.c','drivers/usb/xhci_test_report.c',
            'drivers/usb/xhci_enumerate.c','drivers/usb/usb_descriptors.c','drivers/storage/usb_bot.c',
            'drivers/storage/brcm_firmware.c','drivers/storage/brcm_start.c','drivers/storage/brcm_control.c','drivers/storage/brcm_prepare.c','drivers/storage/brcm_scan.c','drivers/storage/brcm_auth.c','drivers/storage/brcm_auth_probe.c','drivers/storage/brcm_join_probe.c','drivers/storage/brcm_sae_probe.c','drivers/storage/brcm_assoc.c','drivers/storage/brcm_keys.c','drivers/storage/wifi_target.c','lib/sha256.c','lib/crypto.c','lib/trust_time.c','lib/aes.c','lib/aes_selftest.c','lib/wpa_rsn.c','lib/wpa_keys.c','lib/wpa_probe.c','lib/wpa_group_rekey.c','network/dhcp.c','network/netprobe.c','network/udp.c','network/tcp.c','network/dns.c','network/dns_selftest.c','network/session.c','network/netprobe_selftest.c','network/dhcp_selftest.c','lib/wpa_retry_selftest.c','lib/wpa_kde.c','lib/wpa_kde_selftest.c','lib/wpa_keys_selftest.c','lib/p256.c','lib/p256_selftest.c','lib/sae_h2e.c','lib/sae_hunt.c','lib/sae_h2e_selftest.c','lib/sae_session.c','lib/sae_session_selftest.c','drivers/random/rng200.c','drivers/random/pi5_rng200.c','kernel/core/wifi_files.c')
        $sources+=@('kernel/core/package.c','lib/ecdsa.c')
        if ($Target -eq 'rpi5') { $sources+=@('drivers/storage/sdhci.c','drivers/storage/sdio.c','drivers/storage/brcm_sdio.c','drivers/storage/brcm_chip.c','drivers/storage/pi5_sd.c','drivers/storage/pi5_sd_probe.c','drivers/storage/pi5_wifi_probe.c','drivers/storage/pi5_wifi_power.c','drivers/usb/pi5_dma.c','drivers/usb/pi5_xhci_test.c') }
        else { $sources+=@('drivers/storage/virtio_blk.c','drivers/usb/qemu_xhci_test.c','tests/udp_worker.c') }
        $sources+=@('drivers/display/simple_framebuffer.c','drivers/display/framebuffer_terminal.c','kernel/core/framebuffer_commands.c')
        $sources+=@('drivers/platform/pi5_health.c','drivers/platform/pi5_mailbox.c','kernel/core/platform_health_commands.c')
        $sources+=@('kernel/core/dt_resources.c','kernel/core/device_inventory.c')
        $sources+=@('kernel/core/resources.c','kernel/core/resource_report.c')
        $sources+=@('kernel/core/rp1_commands.c','kernel/core/rp1_dma_command.c','kernel/core/pcie_commands.c')
        if ($Target -eq 'rpi5') { $sources+=@('drivers/platform/rp1_probe.c','drivers/platform/rp1_dma_test.c','drivers/platform/rp1_msix.c','drivers/platform/rp1_eth.c','drivers/platform/rp1_eth_loop.c','network/ethernet_ipv4.c','drivers/platform/pi5_pcie.c') }
        if ($Target -ne 'rpi5') { $sources+='kernel/core/fdt.c' }
        $objects = @()
        foreach ($source in $sources) {
            $object = "$out/" + ($source -replace '[/\\.]','_') + '.o'
            # Keep the Pi kernel below the firmware DTB boundary.
            $optimize=if ($source -in @('kernel/core/trust_time_commands.c','lib/trust_time.c','network/tcp.c','network/netprobe.c','network/udp.c','drivers/storage/brcm_control.c','drivers/storage/brcm_assoc.c','drivers/storage/pi5_wifi_probe.c',
                'drivers/display/simple_framebuffer.c','drivers/display/framebuffer_terminal.c','kernel/core/framebuffer_commands.c','kernel/core/dt_property.c','kernel/core/main.c',
                'drivers/platform/pi5_health.c','drivers/platform/pi5_mailbox.c','kernel/core/platform_health_commands.c',
                'kernel/core/dt_resources.c','kernel/core/device_inventory.c','kernel/core/console.c','kernel/memory/boot_memory.c',
                'kernel/core/resources.c','kernel/core/resource_report.c','kernel/core/app_files.c','kernel/core/storage_commands.c',
                'kernel/core/rp1_commands.c','drivers/platform/rp1_probe.c','kernel/core/rp1_dma_command.c','drivers/platform/rp1_dma_test.c',
                'filesystem/vfs.c','filesystem/fat32.c','filesystem/kfile.c','filesystem/ramfs.c',
                'arch/aarch64/interrupts/irq.c','drivers/irq/gic.c','drivers/irq/gic_dt.c',
                'drivers/platform/rp1_msix.c','drivers/usb/pi5_usb_dt.c','kernel/core/usb_commands.c',
                'drivers/usb/xhci_observe.c','drivers/usb/xhci_test_report.c','drivers/usb/pi5_xhci_test.c','drivers/usb/usb_descriptors.c',
                'kernel/scheduler/tasks.c','kernel/scheduler/network_worker.c','kernel/memory/pages.c','drivers/storage/usb_bot.c','network/session.c',
                'kernel/memory/map.c','drivers/storage/sdhci.c','drivers/storage/pi5_sd.c','drivers/storage/pi5_sd_probe.c',
                'kernel/core/process_commands.c','kernel/process/user_heap.c','kernel/process/elf_app.c',
                'kernel/memory/mmu_tables.c','kernel/memory/heap.c',
                'kernel/memory/heap_commands.c','drivers/storage/brcm_scan.c','drivers/storage/brcm_firmware.c',
                'drivers/storage/sdio.c','drivers/platform/rp1_eth.c','drivers/platform/rp1_eth_loop.c','drivers/storage/brcm_sdio.c',
                'drivers/storage/brcm_auth.c','drivers/storage/brcm_auth_probe.c','drivers/storage/brcm_join_probe.c','drivers/storage/brcm_sae_probe.c',
                'arch/aarch64/mmu/runtime.c','drivers/random/pi5_rng200.c','drivers/reset/reset_dt.c','drivers/storage/wifi_target.c','drivers/storage/brcm_chip.c',
                'network/dns.c','kernel/process/app_env.c','kernel/process/app_profile.c','drivers/storage/pi5_wifi_power.c',
                'lib/wpa_probe.c','lib/wpa_kde.c','lib/wpa_rsn.c','lib/aes_selftest.c','lib/p256_selftest.c','lib/sae_session_selftest.c',
                'lib/sae_h2e_selftest.c','lib/wpa_retry_selftest.c','lib/wpa_keys_selftest.c','drivers/storage/block.c','kernel/interrupts/exception.c',
                'kernel/core/fs_commands.c','kernel/core/file_commands.c','kernel/memory/fdt_memory.c','network/dhcp.c',
                'filesystem/persist.c','kernel/process/process.c','drivers/usb/xhci_enumerate.c','drivers/usb/xhci_test.c',
                'arch/aarch64/boot/cpu_commands.c','kernel/core/cpu_topology.c') -or
                ($Target -eq 'rpi5' -and $source -in @(
                'network/ethernet_ipv4.c','drivers/platform/pi5_pcie.c','kernel/core/pcie_commands.c',
                'drivers/storage/brcm_prepare.c','drivers/storage/brcm_start.c',
                'drivers/storage/brcm_keys.c','drivers/random/rng200.c','kernel/core/console_input.c',
                'kernel/core/wifi_files.c','kernel/process/app_args.c','network/netprobe_selftest.c',
                'kernel/core/reboot.c','drivers/usb/pi5_dma.c','kernel/scheduler/policy.c','kernel/process/user_mmu.c',
                'network/dhcp_selftest.c','lib/crypto.c','kernel/interrupts/irq_work.c','lib/aes.c','lib/sha256.c'))) { @('-Os') } else { @() }
            if ($Target -eq 'rpi5' -and $source -eq 'kernel/interrupts/exception.c') {
                $optimize=@($optimize)+@('-fno-tree-switch-conversion')
            }
            # Keep the diagnostic atomics valid on the Cortex-A53 handoff test.
            $sourceCpu=if ($source -eq 'arch/aarch64/boot/cpu_commands.c') { 'cortex-a53' } else { $cpu }
            Invoke-Tool gcc ($common + $optimize + @("-mcpu=$sourceCpu",'-c',$source,'-o',$object))
            $objects += $object
        }
        Invoke-Tool ld ($linkOptions + @('--build-id=none','--no-undefined','--gc-sections','-T',$linker,"-Map=$out/kernel.map",'-o',"$out/kernel.elf") + $objects)
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
            $span=[BitConverter]::ToUInt64($bytes,16)
            Write-Output "Pi 5 kernel span: $span bytes. Space before firmware DTB: $(0x180000-$span) bytes."
            if (!$KeepBuildFiles) { & "$PSScriptRoot/clean-build.ps1" }
            Write-Output "Built Pi 5 candidate: $out/ouros-rpi5.img. Console=$Console. Hardware boot remains UNTESTED."
        } else { Write-Output "Built $out/kernel.elf for QEMU only." }
    }
} finally { Pop-Location }
