[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/wifi-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/sdio_host.c drivers/storage/sdio.c -o build/wifi-tests/test-sdio.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO test compilation failed' }
    & ./build/wifi-tests/test-sdio.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDIO tests failed' }
} finally { Pop-Location }
