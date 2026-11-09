[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/sae_h2e_vectors.mjs build/wifi-tests | Set-Content -Encoding ASCII build/wifi-tests/h2e-public-fixture.json
    if ($LASTEXITCODE -ne 0) { throw 'H2E independent fixture generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/sae_h2e_host.c lib/sae_h2e.c lib/sae_h2e_selftest.c lib/p256.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-sae-h2e.exe
    if ($LASTEXITCODE -ne 0) { throw 'H2E host compilation failed' }
    & ./build/wifi-tests/test-sae-h2e.exe build/wifi-tests
    if ($LASTEXITCODE -ne 0) { throw 'H2E host tests failed' }
} finally { Pop-Location }
