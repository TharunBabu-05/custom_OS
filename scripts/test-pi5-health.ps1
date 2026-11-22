[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=(New-Item -ItemType Directory -Force build/platform-tests).FullName
    $fixture=Join-Path $out 'pi5-health-host.exe'
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/pi5_health_host.c drivers/platform/pi5_health.c drivers/platform/pi5_mailbox.c kernel/core/dt_property.c -o $fixture
    if ($LASTEXITCODE) { throw 'Pi 5 health native test build failed' }
    & $fixture .dist/rpi5-firmware/bcm2712-rpi-5-b.dtb
    if ($LASTEXITCODE) { throw 'Pi 5 health native tests failed' }
} finally { Pop-Location }
