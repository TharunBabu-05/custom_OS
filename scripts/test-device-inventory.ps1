[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=(New-Item -ItemType Directory -Force build/platform-tests).FullName
    $fixture=Join-Path $out 'device-inventory-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/device_inventory_host.c kernel/core/dt_property.c kernel/core/dt_resources.c kernel/core/device_inventory.c kernel/core/cpu_topology.c drivers/reset/reset_dt.c -o $fixture
    if ($LASTEXITCODE) { throw 'Device inventory native test build failed' }
    & $fixture .dist/rpi5-firmware/bcm2712-rpi-5-b.dtb
    if ($LASTEXITCODE) { throw 'Device inventory native tests failed' }
} finally { Pop-Location }
