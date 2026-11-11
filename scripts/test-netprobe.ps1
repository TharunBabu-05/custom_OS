[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & node tests/netprobe_vectors.mjs build/wifi-tests/netprobe-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'ARP/ICMP reference generation failed' }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/netprobe_host.c network/netprobe.c network/netprobe_selftest.c -o build/wifi-tests/test-netprobe.exe
    if ($LASTEXITCODE -ne 0) { throw 'ARP/ICMP model compilation failed' }
    & ./build/wifi-tests/test-netprobe.exe build/wifi-tests/netprobe-vectors.txt
    if ($LASTEXITCODE -ne 0) { throw 'ARP/ICMP model failed' }
} finally { Pop-Location }
