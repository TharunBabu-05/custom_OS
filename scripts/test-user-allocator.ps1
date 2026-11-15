[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/user-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude -Iuser/runtime tests/user_allocator_host.c user/runtime/allocator.c -o build/user-tests/test-user-allocator.exe
    if ($LASTEXITCODE -ne 0) { throw 'User byte allocator test compilation failed' }
    & ./build/user-tests/test-user-allocator.exe
    if ($LASTEXITCODE -ne 0) { throw 'User byte allocator tests failed' }
} finally { Pop-Location }
