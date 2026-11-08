# Produces project-local files only. Does not enumerate or write storage devices.
[CmdletBinding()]
param([ValidateSet('debug','header')][string]$Console = 'debug')
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
$piProfile=Get-Rpi5Profile $Console
$assets=Get-Content -Raw (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
$cache=Join-Path $ProjectRoot '.dist/rpi5-firmware'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
foreach ($asset in $assets.files) {
    $path=Join-Path $cache $asset.name
    if (!(Test-Path -LiteralPath $path)) {
        $url="https://raw.githubusercontent.com/raspberrypi/firmware/$($assets.revision)/boot/$($asset.name)"
        & "$env:SystemRoot/System32/curl.exe" --fail --location --silent --show-error --retry 2 --output "$path.part" $url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $url" }
        if ((Get-FileHash -LiteralPath "$path.part" -Algorithm SHA256).Hash -ne $asset.sha256) {
            throw "Download checksum mismatch: $($asset.name)"
        }
        Move-Item -LiteralPath "$path.part" -Destination $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.sha256) {
        throw "Cached asset checksum mismatch: $($asset.name)"
    }
}
# Always rebuild so the bundle cannot silently contain a stale kernel.
& "$PSScriptRoot/build.ps1" -Target rpi5 -Console $Console
$out=Join-Path $ProjectRoot $piProfile.Output
$stage=Join-Path $out ('boot-files-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $out 'ouros-rpi5.img') -Destination $stage
Copy-Item -LiteralPath (Join-Path $ProjectRoot $piProfile.Config) -Destination (Join-Path $stage 'config.txt')
Copy-Item -LiteralPath (Join-Path $ProjectRoot $piProfile.Readme) -Destination (Join-Path $stage 'README.txt')
foreach ($asset in $assets.files) { Copy-Item -LiteralPath (Join-Path $cache $asset.name) -Destination $stage }
$records=@(Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object {
    [ordered]@{name=$_.Name; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$manifest=[ordered]@{
    board='Raspberry Pi 5 Model B'; hardware_status='UNTESTED'; format='boot-files-not-disk-image'
    kernel_load_address='0x80000'; dtb_address='0x200000'; uart=$piProfile.Uart; console=$Console
    asset_repository=$assets.repository; asset_revision=$assets.revision
    asset_source_url="https://github.com/raspberrypi/firmware/tree/$($assets.revision)/boot"
    files=$records
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -Encoding ASCII (Join-Path $stage 'manifest.json')
$records | ForEach-Object { "$($_.sha256.ToLowerInvariant())  $($_.name)" } |
    Set-Content -Encoding ASCII (Join-Path $stage 'SHA256SUMS')
& "$PSScriptRoot/verify-rpi5-package.ps1" -Directory $stage
$zip=Join-Path $out 'ouros-rpi5-boot-files.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
# Verify the actual archive after extracting it into a new project-local directory.
$roundtrip=Join-Path $out ('archive-check-'+[guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $zip -DestinationPath $roundtrip
& "$PSScriptRoot/verify-rpi5-package.ps1" -Directory $roundtrip
Write-Output "Created $zip"
Write-Output 'Boot files only; physical boot remains UNTESTED. No SD card has been written.'
