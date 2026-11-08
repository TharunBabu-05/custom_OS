[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/memory-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/memory_host.c kernel/memory/fdt_memory.c kernel/memory/map.c kernel/memory/pages.c -o build/memory-tests/test-memory.exe
    if ($LASTEXITCODE -ne 0) { throw 'Memory host test compilation failed' }
    $dtbs=@('.dist/rpi5-firmware/bcm2712-rpi-5-b.dtb','build/qemu_virt/virt.dtb') | Where-Object { Test-Path -LiteralPath $_ }
    & ./build/memory-tests/test-memory.exe @dtbs
    if ($LASTEXITCODE -ne 0) { throw 'Memory host tests failed' }
} finally { Pop-Location }
