[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/app-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/app_args_host.c kernel/process/app_args.c -o build/app-tests/test-app-args.exe
    if ($LASTEXITCODE -ne 0) { throw 'Argument test compilation failed' }
    & ./build/app-tests/test-app-args.exe
    if ($LASTEXITCODE -ne 0) { throw 'Argument tests failed' }
} finally { Pop-Location }
