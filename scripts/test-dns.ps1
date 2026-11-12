[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/dns_vectors.mjs build/wifi-tests/dns-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'DNS reference generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/dns_host.c network/dns.c network/dns_selftest.c network/netprobe.c -o build/wifi-tests/test-dns.exe
    if ($LASTEXITCODE -ne 0) { throw 'DNS compilation failed' }
    & ./build/wifi-tests/test-dns.exe build/wifi-tests/dns-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'DNS tests failed' }
} finally { Pop-Location }
