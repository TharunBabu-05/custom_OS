[CmdletBinding()]
param([string]$Image='build/rpi5-header/OurOS-Pi5-header-experimental-sd.img',
      [string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/storage-tests | Out-Null
    $sources=@('tests/storage_host.c','drivers/storage/block.c','drivers/storage/sdhci.c',
        'filesystem/fat32.c','filesystem/ramfs.c','filesystem/vfs.c','kernel/core/dt_property.c')
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude @sources -o build/storage-tests/test-storage.exe
    if ($LASTEXITCODE -ne 0) { throw 'Storage host test compilation failed' }
    & ./build/storage-tests/test-storage.exe $Image
    if ($LASTEXITCODE -ne 0) { throw 'Storage host tests failed' }
} finally { Pop-Location }
