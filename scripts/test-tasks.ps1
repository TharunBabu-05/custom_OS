[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/task-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/tasks_host.c kernel/scheduler/policy.c -o build/task-tests/test-tasks.exe
    if ($LASTEXITCODE -ne 0) { throw 'Task policy compilation failed' }
    & ./build/task-tests/test-tasks.exe
    if ($LASTEXITCODE -ne 0) { throw 'Task policy tests failed' }
} finally { Pop-Location }
