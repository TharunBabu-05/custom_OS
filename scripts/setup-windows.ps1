# Portable tools only: writes .dist; no WSL, registry or physical-device changes.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root '.dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$curl = Join-Path $env:SystemRoot 'System32/curl.exe'
function Fetch([string]$Url, [string]$Name) {
    $destination = Join-Path $dist $Name
    if (!(Test-Path -LiteralPath $destination)) {
        & $curl --fail --location --silent --show-error --retry 2 --output "$destination.part" $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
        Move-Item -LiteralPath "$destination.part" -Destination $destination
    }
}
function Check-Hash([string]$Name, [string]$Checksum, [string]$Algorithm, [int]$Digits) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $dist $Checksum)
    $match = [regex]::Match($text, "(?i)(?<![a-f0-9])[a-f0-9]{$Digits}(?![a-f0-9])")
    if (!$match.Success) { throw "No $Algorithm digest in $Checksum" }
    $actual = (Get-FileHash -LiteralPath (Join-Path $dist $Name) -Algorithm $Algorithm).Hash
    if ($actual -ne $match.Value) { throw "Checksum mismatch: $Name. Move the bad download aside and retry." }
}
$arm = 'https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-aarch64-none-elf.zip'
$qemu = 'https://qemu.weilnetz.de/w64/qemu-w64-setup-20260811'
Fetch $arm 'arm-gnu.zip'
Fetch "$arm.sha256asc" 'arm-gnu.sha256asc'
Check-Hash 'arm-gnu.zip' 'arm-gnu.sha256asc' 'SHA256' 64
Fetch "$qemu.exe" 'qemu-setup.exe'
Fetch "$qemu.sha512" 'qemu.sha512'
Check-Hash 'qemu-setup.exe' 'qemu.sha512' 'SHA512' 128
Fetch 'https://github.com/ip7z/7zip/releases/download/26.06/7zr.exe' '7zr.exe'
Fetch 'https://github.com/ip7z/7zip/releases/download/26.06/7z2606-x64.exe' '7zip-setup.exe'
$extractorHashes = @{
    '7zr.exe' = 'E80DB2DAD715A6C5ADABE292D31ED54E11F4E9B99CA349827B50573B175891BC'
    '7zip-setup.exe' = '9B697F65ECC2D9B0152F5EE96D72DDE522A7827690349D9307935DACF40F42EC'
}
foreach ($name in $extractorHashes.Keys) {
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dist $name)).Hash -ne $extractorHashes[$name]) {
        throw "Extractor checksum mismatch: $name"
    }
}
$seven = Join-Path $dist '7zip/7z.exe'
if (!(Test-Path -LiteralPath $seven)) {
    & (Join-Path $dist '7zr.exe') x (Join-Path $dist '7zip-setup.exe') "-o$dist/7zip" -y
    if ($LASTEXITCODE -ne 0) { throw '7-Zip extraction failed' }
}
$gcc = Join-Path $dist 'arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf-gcc.exe'
if (!(Test-Path -LiteralPath $gcc)) {
    & $seven x (Join-Path $dist 'arm-gnu.zip') "-o$dist" -y
    if ($LASTEXITCODE -ne 0) { throw 'GNU toolchain extraction failed' }
}
$qemuExe = Join-Path $dist 'qemu/qemu-system-aarch64.exe'
if (!(Test-Path -LiteralPath $qemuExe)) {
    & $seven x (Join-Path $dist 'qemu-setup.exe') "-o$dist/qemu" -y
    if ($LASTEXITCODE -ne 0) { throw 'QEMU extraction failed' }
}
& $gcc --version
if ($LASTEXITCODE -ne 0) { throw 'GCC cannot run' }
& $qemuExe --version
if ($LASTEXITCODE -ne 0) { throw 'QEMU cannot run' }
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dist 'arm-gnu.zip'),(Join-Path $dist 'qemu-setup.exe'),(Join-Path $dist '7zr.exe'),(Join-Path $dist '7zip-setup.exe') |
    Select-Object Path,Algorithm,Hash | ConvertTo-Json | Set-Content -Encoding ASCII (Join-Path $dist 'downloads.json')
Write-Output 'Portable tools ready. Run: powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Target check'
