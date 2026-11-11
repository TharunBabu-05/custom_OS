[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/sae_hunt_vectors.mjs build/wifi-tests
    if ($LASTEXITCODE -ne 0) { throw 'Independent H&P vector generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/sae_hunt_host.c lib/sae_hunt.c lib/p256.c lib/sae_session.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-sae-hunt.exe
    if ($LASTEXITCODE -ne 0) { throw 'H&P host compilation failed' }
    & ./build/wifi-tests/test-sae-hunt.exe build/wifi-tests
    if ($LASTEXITCODE -ne 0) { throw 'H&P tests failed' }
} finally { Pop-Location }
