[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$assets=Get-Content -Raw (Join-Path $root 'boot/rpi5/assets.json') | ConvertFrom-Json
$manifest=Get-Content -Raw (Join-Path $Directory 'manifest.json') | ConvertFrom-Json
. "$PSScriptRoot/rpi5-profile.ps1"
# Packages produced before console variants existed use the debug connector.
$console=if ($manifest.PSObject.Properties['console']) { $manifest.console } else { 'debug' }
$piProfile=Get-Rpi5Profile $console
$required=@('COPYING.linux','LICENCE.broadcom','README.txt','bcm2712-rpi-5-b.dtb','config.txt','ouros-rpi5.img')
if ($manifest.asset_revision -ne $assets.revision -or $manifest.hardware_status -ne 'UNTESTED' -or
    $manifest.format -ne 'boot-files-not-disk-image') { throw 'Unexpected package identity' }
$names=@($manifest.files | ForEach-Object { $_.name })
if ($names.Count -ne $required.Count -or (Compare-Object ($names | Sort-Object) ($required | Sort-Object))) {
    throw 'Unexpected manifest file list'
}
$actual=@(Get-ChildItem -LiteralPath $Directory | ForEach-Object { $_.Name })
if (Compare-Object ($actual | Sort-Object) (($required+@('manifest.json','SHA256SUMS')) | Sort-Object)) {
    throw 'Unexpected package directory contents'
}
foreach ($entry in $manifest.files) {
    $path=Join-Path $Directory $entry.name
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Package checksum/length mismatch: $($entry.name)"
    }
}
foreach ($asset in $assets.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $Directory $asset.name) -Algorithm SHA256).Hash -ne $asset.sha256) {
        throw "Pinned asset mismatch: $($asset.name)"
    }
}
$sums=@(Get-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS'))
$expected=@($manifest.files | ForEach-Object { "$($_.sha256.ToLowerInvariant())  $($_.name)" })
if ($sums.Count -ne $expected.Count -or (Compare-Object $sums $expected)) { throw 'SHA256SUMS mismatch' }
$kernel=[IO.File]::ReadAllBytes((Join-Path (Resolve-Path $Directory).Path 'ouros-rpi5.img'))
if ($kernel.Length -lt 64 -or [BitConverter]::ToUInt32($kernel,56) -ne 0x644d5241 -or
    [BitConverter]::ToUInt64($kernel,8) -ne 0x80000 -or
    [BitConverter]::ToUInt64($kernel,16) -lt $kernel.Length -or
    [BitConverter]::ToUInt64($kernel,16) -ge 0x180000) { throw 'Invalid kernel Image header' }
$config=Get-Content -LiteralPath (Join-Path $Directory 'config.txt')
$isHeader=[Text.Encoding]::ASCII.GetString($kernel).Contains("rpi5-header`0")
if ($isHeader -ne ($console -eq 'header') -or $manifest.uart -cne $piProfile.Uart) { throw 'Kernel/console profile mismatch' }
$source=Get-Content -LiteralPath (Join-Path $root $piProfile.Config)
if (($config -join "`n") -cne ($source -join "`n")) { throw 'Boot config differs from reviewed source' }
Write-Output 'PASS: Pi 5 package files, hashes, config and Image header; hardware remains UNTESTED.'
