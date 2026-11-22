[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $null=New-Item -ItemType Directory -Force build/persist-tests
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/persist_v5_host.c filesystem/persist.c drivers/storage/block.c lib/sha256.c -o build/persist-tests/test-v5.exe
    if ($LASTEXITCODE) { throw 'v5 test compilation failed' }
    & ./build/persist-tests/test-v5.exe
    if ($LASTEXITCODE) { throw 'v5 persistence tests failed' }
} finally { Pop-Location }
