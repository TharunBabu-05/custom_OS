[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=(New-Item -ItemType Directory -Force build/platform-tests).FullName
    $fixture=Join-Path $out 'rp1-probe-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/rp1_probe_host.c drivers/platform/rp1_probe.c drivers/platform/rp1_msix.c drivers/platform/rp1_eth.c drivers/platform/pi5_pcie.c drivers/usb/pi5_dma.c drivers/usb/pi5_usb_dt.c kernel/core/dt_property.c kernel/core/dt_resources.c -o $fixture
    if ($LASTEXITCODE) { throw 'RP1 probe native test build failed' }
    & $fixture .dist/rpi5-firmware/bcm2712-rpi-5-b.dtb
    if ($LASTEXITCODE) { throw 'RP1 probe native tests failed' }
    $dmaFixture=Join-Path $out 'rp1-dma-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/rp1_dma_host.c drivers/platform/rp1_dma_test.c -o $dmaFixture
    if ($LASTEXITCODE) { throw 'RP1 DMA native test build failed' }
    & $dmaFixture
    if ($LASTEXITCODE) { throw 'RP1 DMA native tests failed' }
    $msixFixture=Join-Path $out 'rp1-msix-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/rp1_msix_host.c drivers/platform/rp1_msix.c kernel/core/dt_property.c -o $msixFixture
    if ($LASTEXITCODE) { throw 'RP1 MSI-X native test build failed' }
    & $msixFixture
    if ($LASTEXITCODE) { throw 'RP1 MSI-X native tests failed' }
    $socketSources=@('network/udp.c','network/tcp.c','network/dhcp.c','network/netprobe.c','network/dns.c','network/ethernet_ipv4.c','lib/crypto.c','lib/sha256.c')
    $ethFixture=Join-Path $out 'rp1-eth-loop-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/rp1_eth_loop_host.c drivers/platform/rp1_eth_loop.c @socketSources -o $ethFixture
    if ($LASTEXITCODE) { throw 'GEM loopback native test build failed' }
    & $ethFixture
    if ($LASTEXITCODE) { throw 'GEM loopback native tests failed' }
    $udpFixture=Join-Path $out 'rp1-eth-udp-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/rp1_eth_udp_host.c drivers/platform/rp1_eth_loop.c @socketSources -o $udpFixture
    if ($LASTEXITCODE) { throw 'GEM UDP native test build failed' }
    & $udpFixture
    if ($LASTEXITCODE) { throw 'GEM UDP native tests failed' }
} finally { Pop-Location }
