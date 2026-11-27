# Preview, install, or restore files on an existing Pi 5 header boot volume.
[CmdletBinding(DefaultParameterSetName='Update')]
param(
    [Parameter(Mandatory=$true)][string]$BootDirectory,
    [Parameter(ParameterSetName='Update')][string]$Package,
    [Parameter(Mandatory=$true,ParameterSetName='Restore')][string]$RestoreFrom,
    [switch]$Apply
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
. "$PSScriptRoot/rpi5-update-files.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
$project=Split-Path $PSScriptRoot -Parent
$boot=(Get-Item -LiteralPath $BootDirectory).FullName
$bootPrefix=$boot.TrimEnd('\','/')+'\'
if (!$Package) { $Package=Join-Path $project 'build/rpi5-header/OurOS-Pi5-header-boot-update.zip' }

function Get-BootFile([string]$Name) {
    $path=Get-ArtifactPath (Join-Path $boot $Name) $boot
    if (Test-Path -LiteralPath $path -PathType Container) { throw "Expected a boot file at $path" }
    return $path
}

function Get-InstalledHash([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { Get-Rpi5UpdateHash $Path }
}

function Copy-VerifiedFile([string]$Source, [string]$Destination, [string]$Hash) {
    $inputStream=[IO.File]::OpenRead($Source)
    try {
        $outputStream=[IO.File]::Open($Destination,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {
            $inputStream.CopyTo($outputStream)
            $outputStream.Flush($true)
        } finally { $outputStream.Dispose() }
    } finally { $inputStream.Dispose() }
    if ((Get-Rpi5UpdateHash $Destination) -cne $Hash) { throw "Readback checksum mismatch: $Destination" }
}

$hasher=[Security.Cryptography.SHA256]::Create()
try { $lockId=[BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($bootPrefix.ToUpperInvariant()))).Replace('-','') }
finally { $hasher.Dispose() }
$mutex=[Threading.Mutex]::new($false,('Local\OurOS-Boot-'+$lockId))
$locked=$false
$work=$null
try {
    try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
    if (!$locked) { throw "Another updater is using $boot" }
    if ($RestoreFrom) {
        $source=(Get-Item -LiteralPath $RestoreFrom).FullName
        if (($source.TrimEnd('\','/')+'\').StartsWith($bootPrefix,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Use a backup folder outside the boot directory'
        }
        $manifestPath=Get-ArtifactPath (Join-Path $source 'rollback.json') $source
        $manifest=Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
        Assert-Rpi5UpdateManifest $manifest 'OurOS boot rollback v1'
    } else {
        $work=Get-ArtifactPath (Join-Path $project ('build/boot-update-'+[guid]::NewGuid().ToString('N'))) $project
        if ($work.StartsWith($bootPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose the FAT boot root, not the workspace' }
        $null=New-Item -ItemType Directory -Path $work
        $source=$work
        $manifest=Expand-Rpi5UpdatePackage (Resolve-Path -LiteralPath $Package).Path $source
    }

    $config=Get-BootFile 'CONFIG.TXT'
    if (!(Test-Path -LiteralPath $config -PathType Leaf) -or
        (Get-InstalledHash (Get-BootFile 'PI5.DTB')) -cne $manifest.dtb_sha256) {
        throw "CONFIG.TXT or PI5.DTB on $boot differs from the supported Pi 5 header boot profile"
    }
    $installedConfigHash=Get-Rpi5ConfigHash $config
    if ($installedConfigHash -cne $manifest.config_sha256) {
        # A boot file update can keep the exact legacy profile when the new
        # Image still fits below its firmware DTB. Reject other config edits.
        $original=Get-Content -Raw -LiteralPath $config
        $upgraded=$original -creplace '(?m)^device_tree_address=0x200000(?=\r?$)', 'device_tree_address=0x02000000' `
            -creplace '(?m)^device_tree_end=0x300000(?=\r?$)', 'device_tree_end=0x02200000'
        if ($RestoreFrom -or $upgraded -ceq $original) {
            throw "CONFIG.TXT or PI5.DTB on $boot differs from the supported Pi 5 header boot profile"
        }
        $candidate=Join-Path $work 'profile-check.txt'
        [IO.File]::WriteAllText($candidate,$upgraded,[Text.Encoding]::ASCII)
        if ((Get-Rpi5ConfigHash $candidate) -cne $manifest.config_sha256) {
            throw "CONFIG.TXT or PI5.DTB on $boot differs from the supported Pi 5 header boot profile"
        }
        Assert-Rpi5ImageHeader ([IO.File]::ReadAllBytes((Join-Path $source 'OUROS.IMG'))) 0x200000
    }
    if (!$RestoreFrom -and !(Test-Path -LiteralPath (Get-BootFile 'OUROS.IMG') -PathType Leaf)) {
        throw 'Existing OUROS.IMG required for an update. Use the previous backup if recovering an interrupted update.'
    }

    $plan=@(foreach ($file in $manifest.files) {
        $destination=Get-BootFile $file.name
        $current=[string](Get-InstalledHash $destination)
        $expected=[string]$file.sha256
        if (!$RestoreFrom -and $file.name -ceq 'CMDLINE.TXT' -and $current) { continue }
        $payload=Get-ArtifactPath (Join-Path $source $file.name) $source
        if ($expected -and (Get-Rpi5UpdateHash $payload) -cne $expected) { throw "Source checksum mismatch: $($file.name)" }
        if ($current -cne $expected) {
            [pscustomobject]@{ Name=$file.name; Path=$destination; Source=$payload; Before=$current; After=$expected }
        }
    })
    Write-Output "Boot directory $boot"
    if (!$plan.Count) { Write-Output 'All selected boot files already match.'; return }
    foreach ($file in $plan) {
        $action=if (!$file.After) { 'Remove' } elseif (!$file.Before) { 'Add' } else { 'Replace' }
        Write-Output "$action $($file.Name)"
    }
    if (!$Apply) { Write-Output 'Preview only. Add -Apply to write these files.'; return }

    if (!$RestoreFrom) {
        $backup=Get-ArtifactPath (Join-Path $project ('backups/pi5-boot-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))) $project
        $null=New-Item -ItemType Directory -Path $backup
        foreach ($file in $plan) {
            if ($file.Before) { Copy-VerifiedFile $file.Path (Join-Path $backup $file.Name) $file.Before }
        }
        $rollback=[ordered]@{
            format='OurOS boot rollback v1'; board='rpi5-header'
            config_sha256=$installedConfigHash; dtb_sha256=$manifest.dtb_sha256
            files=@($plan | ForEach-Object { @{ name=$_.Name; sha256=$_.Before } })
        }
        $rollbackPath=Join-Path $work 'rollback.json'
        $rollback | ConvertTo-Json -Depth 4 | Set-Content -Encoding ASCII -LiteralPath $rollbackPath
        Assert-Rpi5UpdateManifest (Get-Content -Raw -LiteralPath $rollbackPath | ConvertFrom-Json) 'OurOS boot rollback v1'
        Copy-VerifiedFile $rollbackPath (Join-Path $backup 'rollback.json') (Get-Rpi5UpdateHash $rollbackPath)
        Write-Output "Verified backup: $backup"
    }

    foreach ($file in $plan) {
        if ([string](Get-InstalledHash (Get-BootFile $file.Name)) -cne $file.Before) { throw "File changed during update: $($file.Name)" }
        if ($file.After) {
            Copy-VerifiedFile $file.Source $file.Path $file.After
        } else {
            [IO.File]::Delete($file.Path)
            if (Test-Path -LiteralPath $file.Path) { throw "Could not remove $($file.Name)" }
        }
    }
    Write-Output "Verified $($plan.Count) boot file changes. Safely eject the drive before booting the Pi."
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    if ($work -and (Test-Path -LiteralPath $work)) {
        $checked=Get-ArtifactPath $work (Join-Path $project 'build')
        $null=@(Get-ArtifactFiles $checked)
        Remove-Item -LiteralPath $checked -Recurse -Force
    }
}
