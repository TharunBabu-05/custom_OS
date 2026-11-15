[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/app-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/app_env_host.c kernel/process/app_env.c -o build/app-tests/test-app-env.exe
    if ($LASTEXITCODE -ne 0) { throw 'Environment test compilation failed' }
    & ./build/app-tests/test-app-env.exe
    if ($LASTEXITCODE -ne 0) { throw 'Environment tests failed' }
} finally { Pop-Location }
