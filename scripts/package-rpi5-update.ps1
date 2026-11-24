# Build a paired Pi 5 header kernel and user application update.
[CmdletBinding()]
param([string]$BasePackage)
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
. "$PSScriptRoot/rpi5-update-files.ps1"

$basePath=if ($BasePackage) { (Resolve-Path -LiteralPath $BasePackage).Path }
if (!$basePath) { & "$PSScriptRoot/build.ps1" -Target rpi5 -Console header -KeepBuildFiles }
$profile=Get-Rpi5Profile header
$out=Join-Path $ProjectRoot $profile.Output
$workRelative=$profile.Output+'/archive-check-'+[guid]::NewGuid().ToString('N')
$work=Join-Path $ProjectRoot $workRelative
$stage=Join-Path $work 'payload'
$check=Join-Path $work 'check'
$null=New-Item -ItemType Directory -Path $stage,$check
& "$PSScriptRoot/build-apps.ps1" -OutputDirectory "$workRelative/apps"

if ($basePath) {
    $baseManifest=Expand-Rpi5UpdatePackage $basePath $stage
} else {
    Copy-Item -LiteralPath (Join-Path $out 'ouros-rpi5.img') -Destination (Join-Path $stage 'OUROS.IMG')
    [IO.File]::WriteAllText((Join-Path $stage 'CMDLINE.TXT'),"ouros.startup=shell`n",[Text.Encoding]::ASCII)
}
Get-ChildItem -LiteralPath (Join-Path $work 'apps') -File -Filter '*.ELF' |
    Copy-Item -Destination $stage -Force
Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'user/shell') -File -Filter '*.OSH' |
    Copy-Item -Destination $stage -Force

# Retain the matching profile hashes when pairing rebuilt apps with a base kernel.
if ($basePath) {
    $configHash=$baseManifest.config_sha256
    $dtbHash=$baseManifest.dtb_sha256
} else {
    $config=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot $profile.Config)
    $config=$config.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
    $configPath=Join-Path $work 'CONFIG.TXT'
    [IO.File]::WriteAllText($configPath,$config,[Text.Encoding]::ASCII)
    $configHash=Get-Rpi5ConfigHash $configPath
    $assets=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
    $dtb=$assets.files | Where-Object { $_.name -ceq 'bcm2712-rpi-5-b.dtb' }
    $dtbHash=$dtb.sha256.ToUpperInvariant()
}
$records=@(Get-ChildItem -LiteralPath $stage -File | Where-Object Name -cne 'manifest.json' | Sort-Object Name | ForEach-Object {
    [ordered]@{ name=$_.Name; sha256=(Get-Rpi5UpdateHash $_.FullName) }
})
$manifest=[ordered]@{
    format='OurOS boot package v1'; board='rpi5-header'
    config_sha256=$configHash; dtb_sha256=$dtbHash
    files=$records
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding ASCII -LiteralPath (Join-Path $stage 'manifest.json')
$candidate=Join-Path $work 'candidate.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$candidate)
$null=Expand-Rpi5UpdatePackage $candidate $check
$package=if ($basePath) { Join-Path $out 'OurOS-Pi5-header-apps-boot-update.zip' }
    else { Join-Path $out 'OurOS-Pi5-header-boot-update.zip' }
Move-Item -LiteralPath $candidate -Destination $package -Force
Write-Output "Verified boot-update ZIP: $package"
