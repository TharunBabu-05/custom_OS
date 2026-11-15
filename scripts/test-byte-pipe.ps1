[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/app-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/byte_pipe_host.c kernel/process/byte_pipe.c -o build/app-tests/test-byte-pipe.exe
    if ($LASTEXITCODE -ne 0) { throw 'Pipe test compilation failed' }
    & ./build/app-tests/test-byte-pipe.exe
    if ($LASTEXITCODE -ne 0) { throw 'Pipe tests failed' }
} finally { Pop-Location }
