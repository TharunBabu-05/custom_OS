[CmdletBinding()]
param([string]$HostCompiler='C:\msys64\mingw64\bin\gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/qemu_virt | Out-Null
    & $QemuExe -machine 'virt-9.2,dumpdtb=build/qemu_virt/virt.dtb,virtualization=off,secure=off' -cpu cortex-a53 -m 128 -display none -monitor none -serial null -net none
    if ($LASTEXITCODE -ne 0) { throw 'DTB generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/fdt_host.c kernel/core/fdt.c -o build/qemu_virt/test-fdt.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test build failed' }
    & .\build\qemu_virt\test-fdt.exe build/qemu_virt/virt.dtb
    if ($LASTEXITCODE -ne 0) { throw 'DTB tests failed' }
} finally { Pop-Location }
