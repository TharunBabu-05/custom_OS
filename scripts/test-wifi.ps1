[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & "$PSScriptRoot/test-dhcp.ps1" -HostCompiler $HostCompiler
    & "$PSScriptRoot/test-netprobe.ps1" -HostCompiler $HostCompiler
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_port_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c -o build/wifi-tests/test-brcm-port.exe
    if ($LASTEXITCODE -ne 0) { throw 'Peer port test compilation failed' }
    & ./build/wifi-tests/test-brcm-port.exe
    if ($LASTEXITCODE -ne 0) { throw 'Peer port tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -DNDEBUG -Iinclude tests/host_assert_probe.c -o build/wifi-tests/test-host-assert.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host assertion probe compilation failed' }
    $probeInfo=New-Object System.Diagnostics.ProcessStartInfo
    $probeInfo.FileName=Join-Path $ProjectRoot 'build/wifi-tests/test-host-assert.exe'
    $probeInfo.UseShellExecute=$false; $probeInfo.CreateNoWindow=$true
    $probeInfo.RedirectStandardError=$true
    $probe=New-Object System.Diagnostics.Process; $probe.StartInfo=$probeInfo
    $probeStarted=$false
    try {
        if (!$probe.Start()) { throw 'Cannot start host assertion probe' }; $probeStarted=$true
        $probeOutput=$probe.StandardError.ReadToEndAsync()
        if (!$probe.WaitForExit(3000)) { throw 'Host assertion probe blocked instead of returning failure' }
        if ($probe.ExitCode -ne 1 -or $probeOutput.Result -notmatch 'FAIL: .*host_assert_probe.c:[0-9]+:.*intentional host assertion probe') { throw 'Host assertion failure did not report correctly' }
        $probeOutput.Result | Set-Content -Encoding ASCII build/wifi-tests/host-assert-probe.stderr.log
        Write-Output 'PASS: host assertions return exit 1 with file/line/expression, remain enabled under NDEBUG and finish without an abort dialog'
    } finally {
        if ($probeStarted -and !$probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }
        $probe.Dispose()
    }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/sdio_host.c drivers/storage/sdio.c drivers/storage/brcm_sdio.c -o build/wifi-tests/test-sdio.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO test compilation failed' }
    & ./build/wifi-tests/test-sdio.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_chip_host.c drivers/storage/brcm_chip.c -o build/wifi-tests/test-chip.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi chip discovery test compilation failed' }
    & ./build/wifi-tests/test-chip.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi chip discovery tests failed' }
    . "$PSScriptRoot/wifi-assets.ps1"
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_firmware_host.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-firmware.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi firmware test compilation failed' }
    & ./build/wifi-tests/test-firmware.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi firmware tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_start_host.c drivers/storage/brcm_start.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-start.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi startup test compilation failed' }
    & ./build/wifi-tests/test-start.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi startup tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_control_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c -o build/wifi-tests/test-control.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi control test compilation failed' }
    & ./build/wifi-tests/test-control.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi control tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_glom_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c -o build/wifi-tests/test-glom.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi aggregate test compilation failed' }
    & ./build/wifi-tests/test-glom.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi aggregate tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_iovar_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c drivers/storage/brcm_auth_probe.c drivers/storage/brcm_join_probe.c drivers/storage/brcm_auth.c -o build/wifi-tests/test-iovar.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi IOVAR test compilation failed' }
    & ./build/wifi-tests/test-iovar.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi IOVAR tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_data_tx_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c -o build/wifi-tests/test-brcm-data-tx.exe
    if ($LASTEXITCODE -ne 0) { throw 'EAPOL data transport compilation failed' }
    & ./build/wifi-tests/test-brcm-data-tx.exe
    if ($LASTEXITCODE -ne 0) { throw 'EAPOL data transport tests failed' }
    & "$PSScriptRoot/test-brcm-auth.ps1" -HostCompiler $HostCompiler
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_keys_host.c drivers/storage/brcm_keys.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-brcm-keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'Firmware key driver compilation failed' }
    & ./build/wifi-tests/test-brcm-keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'Firmware key driver tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_prepare_host.c drivers/storage/brcm_prepare.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-prepare.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi preparation test compilation failed' }
    & ./build/wifi-tests/test-prepare.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi preparation tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_scan_host.c drivers/storage/brcm_scan.c drivers/storage/brcm_prepare.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-scan.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi scan test compilation failed' }
    & ./build/wifi-tests/test-scan.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi scan tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/wifi_target_host.c drivers/storage/wifi_target.c drivers/storage/brcm_scan.c drivers/storage/brcm_prepare.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-target.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi target test compilation failed' }
    & ./build/wifi-tests/test-target.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi target tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_scan_transport_host.c drivers/storage/brcm_control.c network/dhcp.c network/netprobe.c drivers/storage/brcm_scan.c drivers/storage/brcm_prepare.c drivers/storage/brcm_firmware.c lib/sha256.c -o build/wifi-tests/test-scan-transport.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi scan transport test compilation failed' }
    & ./build/wifi-tests/test-scan-transport.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi scan transport tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/crypto_host.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-crypto.exe
    if ($LASTEXITCODE -ne 0) { throw 'Crypto test compilation failed' }
    & ./build/wifi-tests/test-crypto.exe
    if ($LASTEXITCODE -ne 0) { throw 'Crypto tests failed' }
    & "$PSScriptRoot/test-p256.ps1" -HostCompiler $HostCompiler
    & "$PSScriptRoot/test-sae-h2e.ps1" -HostCompiler $HostCompiler
    & "$PSScriptRoot/test-sae-hunt.ps1" -HostCompiler $HostCompiler
    & "$PSScriptRoot/test-sae-session.ps1" -HostCompiler $HostCompiler
    & "$PSScriptRoot/test-wpa-keys.ps1" -HostCompiler $HostCompiler
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/rng200_host.c drivers/random/rng200.c drivers/random/pi5_rng200.c kernel/core/dt_property.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-rng.exe
    if ($LASTEXITCODE -ne 0) { throw 'RNG test compilation failed' }
    & ./build/wifi-tests/test-rng.exe
    if ($LASTEXITCODE -ne 0) { throw 'RNG tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/wifi_power_host.c drivers/storage/pi5_wifi_power.c kernel/core/dt_property.c -o build/wifi-tests/test-power.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi board preparation test compilation failed' }
    & ./build/wifi-tests/test-power.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi board preparation tests failed' }
} finally { Pop-Location }
