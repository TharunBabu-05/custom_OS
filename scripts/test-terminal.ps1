[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/app-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/terminal_host.c kernel/core/terminal.c -o build/app-tests/test-terminal.exe
    if ($LASTEXITCODE -ne 0) { throw 'Terminal test compilation failed' }
    & ./build/app-tests/test-terminal.exe
    if ($LASTEXITCODE -ne 0) { throw 'Terminal tests failed' }
} finally { Pop-Location }
