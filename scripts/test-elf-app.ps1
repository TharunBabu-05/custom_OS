[CmdletBinding()]
param([string[]]$Images=@(),[string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    New-Item -ItemType Directory -Force build/app-tests | Out-Null
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Wmissing-prototypes -Iinclude tests/elf_app_host.c kernel/process/elf_app.c -o build/app-tests/test-elf-app.exe
    if ($LASTEXITCODE -ne 0) { throw 'ELF application parser test compilation failed' }
    & ./build/app-tests/test-elf-app.exe @Images
    if ($LASTEXITCODE -ne 0) { throw 'ELF application parser tests failed' }
} finally { Pop-Location }
