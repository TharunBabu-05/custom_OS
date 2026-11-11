[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/p256_field_vectors.mjs build/wifi-tests/p256-field-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'Field fixture generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -DOUROS_P256_FIELD_TEST -Iinclude tests/p256_field_host.c lib/p256.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-p256-field.exe
    if ($LASTEXITCODE -ne 0) { throw 'Field test compilation failed' }
    & ./build/wifi-tests/test-p256-field.exe build/wifi-tests/p256-field-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'Field modulo comparisons failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/p256_host.c lib/p256.c lib/p256_selftest.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-p256.exe
    if ($LASTEXITCODE -ne 0) { throw 'P-256 test compilation failed' }
    & ./build/wifi-tests/test-p256.exe
    if ($LASTEXITCODE -ne 0) { throw 'P-256 known-answer/boundary tests failed' }
    & node tests/p256_vectors.mjs build/wifi-tests/p256-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'OpenSSL fixture generation failed' }
    & ./build/wifi-tests/test-p256.exe build/wifi-tests/p256-public-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'P-256 independent oracle comparison failed' }
} finally { Pop-Location }
