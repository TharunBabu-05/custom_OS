[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/memory-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/mmu_host.c kernel/memory/mmu_tables.c -o build/memory-tests/test-mmu.exe
    if ($LASTEXITCODE -ne 0) { throw 'MMU policy compilation failed' }
    & ./build/memory-tests/test-mmu.exe
    if ($LASTEXITCODE -ne 0) { throw 'MMU policy tests failed' }
} finally { Pop-Location }
