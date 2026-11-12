[CmdletBinding()]
param([string]$Image='build/rpi5-header/OurOS-Pi5-header-experimental-sd.img',
      [string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/file-tests | Out-Null
    $before=(Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash
    $sources=@('tests/kfile_host.c','filesystem/kfile.c','filesystem/vfs.c','filesystem/ramfs.c',
        'filesystem/fat32.c','filesystem/persist.c','drivers/storage/block.c','lib/sha256.c','kernel/memory/heap.c')
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude @sources -o build/file-tests/test-files.exe
    if ($LASTEXITCODE -ne 0) { throw 'Owned file handle test compilation failed' }
    & ./build/file-tests/test-files.exe $Image
    if ($LASTEXITCODE -ne 0) { throw 'Owned file handle tests failed' }
    if ((Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash -ne $before) { throw 'Source image changed' }
    Write-Output 'PASS: source image SHA256 unchanged; tests write only their in-memory copy.'
} finally { Pop-Location }
