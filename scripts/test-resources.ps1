[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=(New-Item -ItemType Directory -Force build/platform-tests).FullName
    $fixture=Join-Path $out 'resources-host.exe'
    & $HostCompiler -std=c11 -Os -Wall -Wextra -Werror -Iinclude tests/resources_host.c kernel/core/resources.c -o $fixture
    if ($LASTEXITCODE) { throw 'Resource registry native test build failed' }
    & $fixture
    if ($LASTEXITCODE) { throw 'Resource registry native tests failed' }
} finally { Pop-Location }
