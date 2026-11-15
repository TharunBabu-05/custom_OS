[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/usb-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/usb_host.c tests/xhci_ring_host.c drivers/usb/xhci_observe.c drivers/usb/xhci_test.c drivers/usb/xhci_enumerate.c drivers/usb/usb_descriptors.c drivers/storage/usb_bot.c drivers/storage/block.c drivers/usb/pi5_usb_dt.c drivers/usb/pi5_dma.c kernel/core/dt_property.c kernel/core/usb_commands.c -o build/usb-tests/test-usb.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB host test compilation failed' }
    & ./build/usb-tests/test-usb.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB host tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/usb_bot_host.c drivers/storage/usb_bot.c drivers/storage/block.c -o build/usb-tests/test-usb-bot.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB BOT test compilation failed' }
    & ./build/usb-tests/test-usb-bot.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB BOT tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/usb_bot_stall_host.c drivers/storage/usb_bot.c drivers/storage/block.c -o build/usb-tests/test-usb-bot-stall.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB BOT stall test compilation failed' }
    & ./build/usb-tests/test-usb-bot-stall.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB BOT stall tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/xhci_stall_host.c drivers/usb/usb_descriptors.c drivers/storage/usb_bot.c drivers/storage/block.c -o build/usb-tests/test-xhci-stall.exe
    if ($LASTEXITCODE -ne 0) { throw 'xHCI stall test compilation failed' }
    & ./build/usb-tests/test-xhci-stall.exe
    if ($LASTEXITCODE -ne 0) { throw 'xHCI stall tests failed' }
    . "$PSScriptRoot/data-format.ps1"
    $fixture=Join-Path $ProjectRoot 'build/usb-tests/blank-v2.bin'
    [IO.File]::WriteAllBytes($fixture,(New-OurOSDataPartition -Version 2))
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/usb_persist_host.c drivers/usb/usb_descriptors.c drivers/storage/usb_bot.c drivers/storage/block.c filesystem/persist.c lib/sha256.c -o build/usb-tests/test-usb-persist.exe
    if ($LASTEXITCODE -ne 0) { throw 'USB persistence integration compilation failed' }
    & ./build/usb-tests/test-usb-persist.exe $fixture
    if ($LASTEXITCODE -ne 0) { throw 'USB persistence integration failed' }
} finally { Pop-Location }
