[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/sae_session_vectors.mjs build/wifi-tests/sae-session-public-vectors.txt | Set-Content -Encoding ASCII build/wifi-tests/sae-session-public-fixture.json
    if ($LASTEXITCODE -ne 0) { throw 'SAE independent fixture generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/sae_session_host.c lib/sae_session.c lib/sae_session_selftest.c lib/p256.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-sae-session.exe
    if ($LASTEXITCODE -ne 0) { throw 'SAE session compilation failed' }
    & ./build/wifi-tests/test-sae-session.exe build/wifi-tests/sae-session-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'SAE exchange tests failed' }
} finally { Pop-Location }
