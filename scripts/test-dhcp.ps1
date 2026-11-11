[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/dhcp_vectors.mjs build/wifi-tests/dhcp-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'DHCP reference packet generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/dhcp_host.c network/dhcp.c network/dhcp_selftest.c -o build/wifi-tests/test-dhcp.exe
    if ($LASTEXITCODE -ne 0) { throw 'DHCP test compilation failed' }
    & ./build/wifi-tests/test-dhcp.exe build/wifi-tests/dhcp-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'DHCP tests failed' }
} finally { Pop-Location }
