[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/network_session_host.c network/session.c network/netprobe.c network/dns.c lib/crypto.c lib/sha256.c -o build/wifi-tests/test-network-session.exe
    if ($LASTEXITCODE -ne 0) { throw 'Network session compilation failed' }
    & ./build/wifi-tests/test-network-session.exe
    if ($LASTEXITCODE -ne 0) { throw 'Network session tests failed' }
} finally { Pop-Location }
