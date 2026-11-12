[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/interrupt-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/tick_host.c kernel/interrupts/tick.c -o build/interrupt-tests/test-tick.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tick arithmetic compilation failed' }
    & ./build/interrupt-tests/test-tick.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tick arithmetic tests failed' }
} finally { Pop-Location }
