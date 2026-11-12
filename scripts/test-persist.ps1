[CmdletBinding()]
param([string]$Image,[string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/storage-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/persist_host.c filesystem/persist.c drivers/storage/block.c lib/sha256.c -o build/storage-tests/test-persist.exe
    if ($LASTEXITCODE -ne 0) { throw 'Persistent filesystem test compilation failed' }
    if ($Image) { & ./build/storage-tests/test-persist.exe $Image }
    else { & ./build/storage-tests/test-persist.exe }
    if ($LASTEXITCODE -ne 0) { throw 'Persistent filesystem tests failed' }
    . "$PSScriptRoot/data-format.ps1"
    $blank=Join-Path $ProjectRoot 'build/storage-tests/blank-data.bin'
    [IO.File]::WriteAllBytes($blank,(New-OurOSDataPartition))
    & ./build/storage-tests/test-persist.exe --partition $blank
    if ($LASTEXITCODE -ne 0) { throw 'Independent PowerShell data-format fixture failed' }
} finally { Pop-Location }
