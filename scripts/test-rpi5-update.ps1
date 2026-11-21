# Exercises boot updates against regular files in the project build tree.
[CmdletBinding()]
param([string]$Package)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
. "$PSScriptRoot/rpi5-update-files.ps1"
$project=Split-Path $PSScriptRoot -Parent
if (!$Package) { $Package=Join-Path $project 'build/rpi5-header/OurOS-Pi5-header-boot-update.zip' }
$packagePath=(Resolve-Path -LiteralPath $Package).Path
$work=Get-ArtifactPath (Join-Path $project ('build/update-test-'+[guid]::NewGuid().ToString('N'))) (Join-Path $project 'build')
$boot=Join-Path $work 'boot'
$stage=Join-Path $work 'package'
$null=New-Item -ItemType Directory -Path $boot,$stage
$backup=$null

function Write-Fixture([string]$Name,[string]$Contents) {
    [IO.File]::WriteAllText((Join-Path $boot $Name),$Contents,[Text.Encoding]::ASCII)
}
function Get-Inventory {
    @(Get-ChildItem -LiteralPath $boot -File | Sort-Object Name | ForEach-Object { "$($_.Name) $(Get-Rpi5UpdateHash $_.FullName)" }) -join "`n"
}
function Assert([bool]$Condition,[string]$Message) {
    if (!$Condition) { throw $Message }
}

try {
    $manifest=Expand-Rpi5UpdatePackage $packagePath $stage
    $config=(Get-Content -Raw -LiteralPath (Join-Path $project 'boot/rpi5/config-header.txt')).
        Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
    Write-Fixture 'CONFIG.TXT' $config
    Copy-Item -LiteralPath (Join-Path $project '.dist/rpi5-firmware/bcm2712-rpi-5-b.dtb') -Destination (Join-Path $boot 'PI5.DTB')
    Write-Fixture 'OUROS.IMG' 'previous kernel'
    Write-Fixture 'SHELL.ELF' 'previous shell'
    Write-Fixture 'MYFILE.TXT' 'local boot volume file'
    $original=Get-Inventory

    $preview=& "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -Package $packagePath
    Assert ($preview -contains 'Replace OUROS.IMG' -and $preview -contains 'Add CMDLINE.TXT') 'Update preview differs'
    Assert ((Get-Inventory) -ceq $original) 'Preview changed the boot directory'

    Write-Fixture 'CONFIG.TXT' 'wrong profile'
    $errorText=try { & "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -Package $packagePath -Apply }
        catch { $_.Exception.Message }
    Assert ($errorText -like '*differs from the supported Pi 5 header boot profile*') 'Wrong profile was accepted'
    Write-Fixture 'CONFIG.TXT' $config

    $corrupt=Join-Path $work 'corrupt.zip'
    Copy-Item -LiteralPath $packagePath -Destination $corrupt
    $zip=[IO.Compression.ZipFile]::Open($corrupt,[IO.Compression.ZipArchiveMode]::Update)
    try {
        $zip.GetEntry('SHELL.ELF').Delete()
        $stream=$zip.CreateEntry('SHELL.ELF').Open()
        try { $stream.WriteByte(0) } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
    $errorText=try { & "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -Package $corrupt -Apply }
        catch { $_.Exception.Message }
    Assert ($errorText -like '*Package checksum mismatch: SHELL.ELF*') 'Corrupted package was accepted'
    Assert ((Get-Inventory) -ceq $original) 'Rejected update changed the boot directory'

    $result=@(& "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -Package $packagePath -Apply)
    $backupLine=$result | Where-Object { $_ -like 'Verified backup: *' }
    Assert ([bool]$backupLine) 'Update did not create a backup'
    $backup=$backupLine.Substring('Verified backup: '.Length)
    foreach ($file in $manifest.files) {
        Assert ((Get-Rpi5UpdateHash (Join-Path $boot $file.name)) -ceq $file.sha256) "Installed $($file.name) differs"
    }
    Assert ([IO.File]::ReadAllText((Join-Path $boot 'MYFILE.TXT')) -ceq 'local boot volume file') 'Unrelated file changed'

    Write-Fixture 'CMDLINE.TXT' 'ouros.startup=monitor'
    $current=Get-Inventory
    $noOp=& "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -Package $packagePath -Apply
    Assert ($noOp -contains 'All selected boot files already match.' -and (Get-Inventory) -ceq $current) 'Repeat update rewrote a boot file'

    Write-Fixture 'OUROS.IMG' 'simulated interrupted write'
    $null=& "$PSScriptRoot/update-rpi5-boot.ps1" -BootDirectory $boot -RestoreFrom $backup -Apply
    Assert ((Get-Inventory) -ceq $original) 'Rollback differs from original inventory'
    Write-Output 'PASS: preview, profile and hash rejection, installation, command-line preservation, repeat update and interrupted-write rollback.'
} finally {
    if ($backup) {
        $checked=Get-ArtifactPath $backup (Join-Path $project 'backups')
        if ((Split-Path $checked -Leaf) -cnotmatch '^pi5-boot-\d{8}-\d{6}-[0-9a-f]{8}$') { throw 'Unexpected test backup path' }
        $null=@(Get-ArtifactFiles $checked)
        Remove-Item -LiteralPath $checked -Recurse -Force
    }
    $checked=Get-ArtifactPath $work (Join-Path $project 'build')
    $null=@(Get-ArtifactFiles $checked)
    Remove-Item -LiteralPath $checked -Recurse -Force
}
