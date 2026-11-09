[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/brcm_auth_host.c drivers/storage/brcm_auth.c drivers/storage/brcm_auth_probe.c -o build/wifi-tests/test-brcm-auth.exe
    if ($LASTEXITCODE -ne 0) { throw 'External-auth transport compilation failed' }
    & ./build/wifi-tests/test-brcm-auth.exe
    if ($LASTEXITCODE -ne 0) { throw 'External-auth transport tests failed' }
} finally { Pop-Location }
