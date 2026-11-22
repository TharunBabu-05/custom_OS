[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/trust-time-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/trust_time_host.c lib/trust_time.c lib/crypto.c lib/sha256.c -o build/trust-time-tests/test.exe
    if ($LASTEXITCODE) { throw 'UTC and trust test compilation failed' }
    & ./build/trust-time-tests/test.exe
    if ($LASTEXITCODE) { throw 'UTC and trust tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/pinned_cert_host.c lib/pinned_cert.c lib/ecdsa.c lib/p256.c lib/trust_time.c lib/crypto.c lib/sha256.c -o build/trust-time-tests/cert.exe
    if ($LASTEXITCODE) { throw 'Pinned certificate test compilation failed' }
    & ./build/trust-time-tests/cert.exe
    if ($LASTEXITCODE) { throw 'Pinned certificate tests failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/tls13_auth_host.c lib/tls13_auth.c lib/tls13_keys.c lib/pinned_cert.c lib/ecdsa.c lib/p256.c lib/trust_time.c lib/crypto.c lib/sha256.c -o build/trust-time-tests/tls13.exe
    if ($LASTEXITCODE) { throw 'TLS transcript test compilation failed' }
    & ./build/trust-time-tests/tls13.exe
    if ($LASTEXITCODE) { throw 'TLS transcript tests failed' }
} finally { Pop-Location }
