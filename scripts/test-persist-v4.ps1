[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/storage-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/persist_v4_host.c filesystem/persist.c drivers/storage/block.c lib/sha256.c -o build/storage-tests/test-persist-v4.exe
    if ($LASTEXITCODE -ne 0) { throw 'v4 persistence test compilation failed' }
    & ./build/storage-tests/test-persist-v4.exe
    if ($LASTEXITCODE -ne 0) { throw 'v4 persistence tests failed' }
} finally { Pop-Location }
