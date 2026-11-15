# Explicit backup conversion to v3/v4; source archives/devices remain untouched.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InputBackup,[Parameter(Mandatory=$true)][string]$Output,
      [ValidateSet(3,4)][int]$TargetVersion=3)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/data-backup.ps1"
. "$PSScriptRoot/disk-image-backups.ps1"
$old=Get-OurOSDataBackup $InputBackup
if ($old.Version -ge $TargetVersion) { throw 'Migration requires an older source; no implicit downgrade or same-format rewrite' }
$destination=Get-ArtifactPath $Output (Split-Path $PSScriptRoot -Parent)
if ([IO.Path]::GetExtension($destination) -ine '.bak' -or (Test-Path -LiteralPath $destination)) { throw 'Choose a new project-local .bak output' }
$files=@()
foreach ($entry in $old.Entries) {
    [byte[]]$data=New-Object byte[] $entry.Bytes
    [Array]::Copy($old.Raw,$entry.DataOffset,$data,0,$data.Length)
    $files+=@{Name=$entry.Name;Data=$data;Slot=$entry.Slot;Parent=$entry.Parent;Directory=$entry.Directory}
}
$temporary=$destination+'.partial-'+[guid]::NewGuid().ToString('N')
try {
    $null=New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($destination))
    [IO.File]::WriteAllBytes($temporary,(New-OurOSDataBackup -Version $TargetVersion -Files $files));$next=Get-OurOSDataBackup $temporary
    foreach ($entry in $old.Entries) {
        $same=@($next.Entries | Where-Object { $_.Path -ceq $entry.Path -and $_.Slot -eq $entry.Slot -and $_.Parent -eq $entry.Parent -and
            $_.Directory -eq $entry.Directory -and $_.Bytes -eq $entry.Bytes -and $_.SHA256 -ceq $entry.SHA256 })
        if ($same.Count -ne 1) { throw 'Migration changed a file' }
    }
    [IO.File]::Move($temporary,$destination)
} finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
Write-Output "MIGRATED: $destination format=$TargetVersion nodes=$($next.Entries.Count) SHA256=$($next.SHA256)"
