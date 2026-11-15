# Explicit v1/v2 flat-file -> v3 root-file archive conversion. No device I/O.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InputBackup,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/data-backup.ps1"
. "$PSScriptRoot/disk-image-backups.ps1"
$old=Get-OurOSDataBackup $InputBackup
if ($old.Version -notin 1,2) { throw 'Migration input must be v1 or v2; v3 is already supported directly' }
$destination=Get-ArtifactPath $Output (Split-Path $PSScriptRoot -Parent)
if ([IO.Path]::GetExtension($destination) -ine '.bak' -or (Test-Path -LiteralPath $destination)) { throw 'Choose a new project-local .bak output' }
$files=@()
foreach ($entry in $old.Entries) {
    [byte[]]$data=New-Object byte[] $entry.Bytes
    [Array]::Copy($old.Raw,$entry.DataOffset,$data,0,$data.Length)
    $files+=@{Name=$entry.Name;Data=$data}
}
$temporary=$destination+'.partial-'+[guid]::NewGuid().ToString('N')
try {
    $null=New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($destination))
    [IO.File]::WriteAllBytes($temporary,(New-OurOSDataBackup -Version 3 -Files $files));$next=Get-OurOSDataBackup $temporary
    foreach ($entry in $old.Entries) {
        $same=@($next.Entries | Where-Object { $_.Name -ceq $entry.Name -and $_.Bytes -eq $entry.Bytes -and $_.SHA256 -ceq $entry.SHA256 })
        if ($same.Count -ne 1) { throw 'Migration changed a file' }
    }
    [IO.File]::Move($temporary,$destination)
} finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
Write-Output "MIGRATED: $destination format=3 files=$($next.Entries.Count) SHA256=$($next.SHA256)"
