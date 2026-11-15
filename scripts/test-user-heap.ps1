[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/user-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/user_heap_host.c kernel/process/user_heap.c kernel/process/user_mmu.c -o build/user-tests/test-user-heap.exe
    if ($LASTEXITCODE -ne 0) { throw 'Private heap model compilation failed' }
    & ./build/user-tests/test-user-heap.exe
    if ($LASTEXITCODE -ne 0) { throw 'Private heap model tests failed' }
} finally { Pop-Location }
