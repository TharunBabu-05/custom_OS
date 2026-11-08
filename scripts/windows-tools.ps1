$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ToolBin = Join-Path $ProjectRoot '.dist/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-aarch64-none-elf/bin'
$QemuExe = Join-Path $ProjectRoot '.dist/qemu/qemu-system-aarch64.exe'
function Invoke-Tool([string]$Name, [string[]]$Arguments) {
    $executable = Join-Path $ToolBin "aarch64-none-elf-$Name.exe"
    if (!(Test-Path -LiteralPath $executable)) { throw 'Run scripts/setup-windows.ps1 first.' }
    & $executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
}
