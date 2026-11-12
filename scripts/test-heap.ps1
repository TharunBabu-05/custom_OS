[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/memory-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/heap_host.c kernel/memory/heap.c -o build/memory-tests/test-heap.exe
    if ($LASTEXITCODE -ne 0) { throw 'Heap host compilation failed' }
    & ./build/memory-tests/test-heap.exe
    if ($LASTEXITCODE -ne 0) { throw 'Heap tests failed' }
    foreach ($case in @('release','duplicate','unaligned')) {
        & ./build/memory-tests/test-heap.exe $case
        if ($LASTEXITCODE -ne 0) { throw "Heap PMM failure case failed: $case" }
    }
} finally { Pop-Location }
