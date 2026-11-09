[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/usb-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/usb_host.c tests/xhci_ring_host.c drivers/usb/xhci_observe.c drivers/usb/xhci_test.c drivers/usb/pi5_usb_dt.c drivers/usb/pi5_dma.c kernel/core/dt_property.c kernel/core/usb_commands.c -o build/usb-tests/test-usb.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB host test compilation failed' }
    & ./build/usb-tests/test-usb.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB host tests failed' }
} finally { Pop-Location }
