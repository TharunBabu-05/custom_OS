[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/irq-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/gic_host.c drivers/irq/gic.c drivers/irq/gic_dt.c kernel/core/dt_property.c -o build/irq-tests/test-gic.exe
    if ($LASTEXITCODE -ne 0) { throw 'GIC host compilation failed' }
    & $QemuExe -machine virt-9.2,virtualization=on,secure=off,dumpdtb=build/irq-tests/virt-el2.dtb -cpu cortex-a76 -smp 1 -m 128 -display none
    if ($LASTEXITCODE -ne 0) { throw 'QEMU EL2 GIC/timer DT generation failed' }
    & ./build/irq-tests/test-gic.exe .dist/rpi5-firmware/bcm2712-rpi-5-b.dtb build/qemu_virt/virt.dtb build/irq-tests/virt-el2.dtb
    if ($LASTEXITCODE -ne 0) { throw 'GIC host tests failed' }
} finally { Pop-Location }
