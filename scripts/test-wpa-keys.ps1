[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/wpa_keys_vectors.mjs build/wifi-tests/wpa-key-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'WPA key fixture generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/wpa_keys_host.c lib/aes.c lib/aes_selftest.c lib/wpa_keys.c lib/wpa_keys_selftest.c lib/wpa_probe.c lib/wpa_retry_selftest.c lib/wpa_kde.c lib/wpa_kde_selftest.c lib/wpa_rsn.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-wpa-keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'WPA key test compilation failed' }
    & ./build/wifi-tests/test-wpa-keys.exe build/wifi-tests/wpa-key-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'WPA key tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/wpa_probe_host.c lib/wpa_probe.c lib/wpa_keys.c lib/aes.c lib/wpa_kde.c lib/wpa_kde_selftest.c lib/wpa_rsn.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-wpa-probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'EAPOL probe compilation failed' }
    & ./build/wifi-tests/test-wpa-probe.exe build/wifi-tests/wpa-key-public-vectors.txt.handshake
    if ($LASTEXITCODE -ne 0) { throw 'EAPOL probe tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/wpa_key_data_host.c lib/wpa_probe.c lib/wpa_keys.c lib/aes.c lib/wpa_kde.c lib/wpa_kde_selftest.c lib/wpa_rsn.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-wpa-key-data.exe
    if ($LASTEXITCODE -ne 0) { throw 'Authenticated key-data compilation failed' }
    & ./build/wifi-tests/test-wpa-key-data.exe build/wifi-tests/wpa-key-public-vectors.txt.keydata
    if ($LASTEXITCODE -ne 0) { throw 'Authenticated key-data tests failed' }
} finally { Pop-Location }
