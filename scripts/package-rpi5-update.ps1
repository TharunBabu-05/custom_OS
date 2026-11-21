# Build a paired Pi 5 header kernel and user application update.
[CmdletBinding()]
param()
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
. "$PSScriptRoot/rpi5-update-files.ps1"

& "$PSScriptRoot/build.ps1" -Target rpi5 -Console header -KeepBuildFiles
$profile=Get-Rpi5Profile header
$out=Join-Path $ProjectRoot $profile.Output
$workRelative=$profile.Output+'/archive-check-'+[guid]::NewGuid().ToString('N')
$work=Join-Path $ProjectRoot $workRelative
$stage=Join-Path $work 'payload'
$check=Join-Path $work 'check'
$null=New-Item -ItemType Directory -Path $stage,$check
& "$PSScriptRoot/build-apps.ps1" -OutputDirectory "$workRelative/apps"

Copy-Item -LiteralPath (Join-Path $out 'ouros-rpi5.img') -Destination (Join-Path $stage 'OUROS.IMG')
Get-ChildItem -LiteralPath (Join-Path $work 'apps') -File -Filter '*.ELF' |
    Copy-Item -Destination $stage
[IO.File]::WriteAllText((Join-Path $stage 'CMDLINE.TXT'),"ouros.startup=shell`n",[Text.Encoding]::ASCII)

# Match the precise FAT boot configuration and pinned DTB for this build.
$config=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot $profile.Config)
$config=$config.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
$configPath=Join-Path $work 'CONFIG.TXT'
[IO.File]::WriteAllText($configPath,$config,[Text.Encoding]::ASCII)
$assets=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
$dtb=$assets.files | Where-Object { $_.name -ceq 'bcm2712-rpi-5-b.dtb' }
$records=@(Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object {
    [ordered]@{ name=$_.Name; sha256=(Get-Rpi5UpdateHash $_.FullName) }
})
$manifest=[ordered]@{
    format='OurOS boot package v1'; board='rpi5-header'
    config_sha256=(Get-Rpi5ConfigHash $configPath); dtb_sha256=$dtb.sha256.ToUpperInvariant()
    files=$records
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding ASCII -LiteralPath (Join-Path $stage 'manifest.json')
$candidate=Join-Path $work 'candidate.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$candidate)
$null=Expand-Rpi5UpdatePackage $candidate $check
$package=Join-Path $out 'OurOS-Pi5-header-boot-update.zip'
Move-Item -LiteralPath $candidate -Destination $package -Force
Write-Output "Verified boot-update ZIP: $package"
