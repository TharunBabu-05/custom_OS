[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_auth_host.c drivers/storage/brcm_auth.c drivers/storage/brcm_auth_probe.c -o build/wifi-tests/test-brcm-auth.exe
    if ($LASTEXITCODE -ne 0) { throw 'External-auth transport compilation failed' }
    & ./build/wifi-tests/test-brcm-auth.exe
    if ($LASTEXITCODE -ne 0) { throw 'External-auth transport tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_join_probe_host.c drivers/storage/brcm_join_probe.c drivers/storage/brcm_auth.c drivers/storage/brcm_auth_probe.c -o build/wifi-tests/test-brcm-join-probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'Join-probe compilation failed' }
    & ./build/wifi-tests/test-brcm-join-probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'Join-probe tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_sae_probe_host.c drivers/storage/brcm_assoc.c network/dhcp.c network/netprobe.c drivers/storage/brcm_keys.c drivers/storage/brcm_sae_probe.c drivers/storage/brcm_join_probe.c drivers/storage/brcm_auth.c drivers/storage/brcm_auth_probe.c lib/wpa_probe.c lib/wpa_keys.c lib/aes.c lib/wpa_kde.c lib/wpa_kde_selftest.c lib/wpa_rsn.c lib/p256.c lib/sae_hunt.c lib/sae_session.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-brcm-sae-probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'SAE commit-probe compilation failed' }
    & ./build/wifi-tests/test-brcm-sae-probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'SAE commit-probe tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/console_input_host.c kernel/core/console_input.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-console-input.exe
    if ($LASTEXITCODE -ne 0) { throw 'Hidden UART input compilation failed' }
    & ./build/wifi-tests/test-console-input.exe
    if ($LASTEXITCODE -ne 0) { throw 'Hidden UART input tests failed' }
} finally { Pop-Location }
