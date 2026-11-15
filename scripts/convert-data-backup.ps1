[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Log,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/data-backup.ps1"
. "$PSScriptRoot/disk-image-backups.ps1"
$inputFile=Get-Item -LiteralPath $Log
if ($inputFile.PSIsContainer -or $inputFile.Length -gt 8MB) { throw 'Expected a plain serial log up to 8 MiB' }
$destination=Get-ArtifactPath $Output (Split-Path $PSScriptRoot -Parent)
if ([IO.Path]::GetExtension($destination) -ine '.bak') { throw 'Output must be a project-local .bak file' }
if (Test-Path -LiteralPath $destination) { throw 'Output already exists; choose a new backup name' }
$lines=[IO.File]::ReadAllLines($inputFile.FullName)
$begin='-----BEGIN OUROS DATA BACKUP V1-----';$end='-----END OUROS DATA BACKUP V1-----'
$active=$false;$done=$false;$started=0;$expected=0;$claimed=$null
$buffer=New-Object byte[] 263680
foreach ($line in $lines) {
    if ($line -ceq $begin) { if ($started++) { throw 'Multiple backup blocks; use a log containing one export' };$active=$true;continue }
    if (!$active) { continue }
    if ($line -ceq $end) { $active=$false;$done=$true;continue }
    if ($line -cmatch '^OUROS-DATA-SHA256 ([0-9a-f]{64})$') { if ($claimed) { throw 'Duplicate backup checksum' };$claimed=$Matches[1];continue }
    if ($claimed -or $line -cnotmatch '^D([0-9a-f]{8}):([0-9a-f]{64})$') { throw 'Invalid/truncated backup record' }
    $at=[Convert]::ToUInt32($Matches[1],16);$hex=$Matches[2]
    if ($at -ne $expected -or $expected+32 -gt $buffer.Length) { throw 'Out-of-order/duplicate/oversize backup record' }
    for ($j=0;$j -lt 32;$j++) { $buffer[$expected+$j]=[Convert]::ToByte($hex.Substring($j*2,2),16) };$expected+=32
}
if (!$done -or $active -or !$claimed -or $expected -notin 33792,132608,263680) { throw 'Incomplete backup; wait for the export end marker' }
[byte[]]$raw=New-Object byte[] $expected;[Array]::Copy($buffer,$raw,$expected)
$sha=[Security.Cryptography.SHA256]::Create()
try { $actual=([BitConverter]::ToString($sha.ComputeHash($raw))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
if ($claimed -cne $actual) { throw 'Serial backup SHA256 mismatch' }
$temporary=$destination+'.partial-'+[guid]::NewGuid().ToString('N')
try {
    $null=New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($destination))
    [IO.File]::WriteAllBytes($temporary,$raw);$backup=Get-OurOSDataBackup $temporary
    # Non-overwriting promotion; a failed export never replaces an earlier backup.
    [IO.File]::Move($temporary,$destination)
} finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
Write-Output "BACKUP: $destination format=$($backup.Version) entries=$($backup.Entries.Count) SHA256=$actual"
