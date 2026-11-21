[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
$ErrorActionPreference='Stop'
Push-Location $ProjectRoot
try {
    $out='build/reboot-tests'
    $null=New-Item -ItemType Directory -Force $out
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/reboot_host.c kernel/core/reboot.c drivers/reset/reset_dt.c kernel/core/dt_property.c -o "$out/test-reboot.exe"
    if ($LASTEXITCODE) { throw 'Reboot host compilation failed' }
    & $QemuExe "-machine" "virt-9.2,virtualization=off,secure=off,dumpdtb=$out/virt-el1.dtb" -cpu cortex-a53 -smp 1 -m 128 -display none
    if ($LASTEXITCODE) { throw 'QEMU EL1 DT generation failed' }
    & $QemuExe "-machine" "virt-9.2,virtualization=on,secure=off,dumpdtb=$out/virt-el2.dtb" -cpu cortex-a76 -smp 1 -m 128 -display none
    if ($LASTEXITCODE) { throw 'QEMU EL2 DT generation failed' }
    & "./$out/test-reboot.exe" .dist/rpi5-firmware/bcm2712-rpi-5-b.dtb "$out/virt-el1.dtb" "$out/virt-el2.dtb"
    if ($LASTEXITCODE) { throw 'Reboot host checks failed' }
} finally { Pop-Location }
