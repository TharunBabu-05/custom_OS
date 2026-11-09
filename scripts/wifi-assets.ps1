# Pinned vendor radio assets only. Downloads/cache are ordinary project files.
. "$PSScriptRoot/windows-tools.ps1"
$WifiAssets=Get-Content -Raw (Join-Path $ProjectRoot 'boot/wifi/assets.json') | ConvertFrom-Json
$WifiCache=Join-Path $ProjectRoot '.dist/wifi-firmware'
New-Item -ItemType Directory -Force $WifiCache | Out-Null
foreach ($asset in $WifiAssets.files) {
    $path=Join-Path $WifiCache $asset.name
    if (!(Test-Path -LiteralPath $path)) {
        $url="https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$($WifiAssets.revision)/$($asset.path)"
        & "$env:SystemRoot/System32/curl.exe" --fail --location --silent --show-error --connect-timeout 15 --max-time 90 --output "$path.part" $url
        if ($LASTEXITCODE -ne 0) { throw "Wi-Fi asset download failed: $($asset.name)" }
        if ((Get-Item -LiteralPath "$path.part").Length -ne $asset.bytes -or (Get-FileHash -LiteralPath "$path.part" -Algorithm SHA256).Hash -ne $asset.sha256) { throw "Wi-Fi asset pin mismatch: $($asset.name)" }
        Move-Item -LiteralPath "$path.part" -Destination $path
    }
    if ((Get-Item -LiteralPath $path).Length -ne $asset.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.sha256) { throw "Cached Wi-Fi asset pin mismatch: $($asset.name)" }
}
