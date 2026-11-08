[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/ramfs-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/ramfs_host.c filesystem/ramfs.c kernel/core/console.c -o build/ramfs-tests/test-ramfs.exe
    if ($LASTEXITCODE -ne 0) { throw 'RAMFS host test compilation failed' }
    & ./build/ramfs-tests/test-ramfs.exe
    if ($LASTEXITCODE -ne 0) { throw 'RAMFS host tests failed' }
} finally { Pop-Location }
