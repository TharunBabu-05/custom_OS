[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/user-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/user_mmu_host.c kernel/process/user_mmu.c -o build/user-tests/test-user-mmu.exe
    if ($LASTEXITCODE -ne 0) { throw 'User MMU host compilation failed' }
    & ./build/user-tests/test-user-mmu.exe
    if ($LASTEXITCODE -ne 0) { throw 'User MMU host tests failed' }
} finally { Pop-Location }
