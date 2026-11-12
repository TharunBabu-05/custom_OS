[CmdletBinding()]
param([ValidateSet('debug','header')][string]$Console = 'debug')
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
$piProfile=Get-Rpi5Profile $Console
$source=Join-Path (Join-Path $ProjectRoot $piProfile.Output) $piProfile.Image
if (!(Test-Path -LiteralPath $source)) { throw 'Run scripts/image-rpi5.ps1 first.' }
$work=Join-Path $ProjectRoot ('build/rpi5/image-negative-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$scratch=Join-Path $work 'corrupt.img'
Copy-Item -LiteralPath $source -Destination $scratch
# Locate OUROS.IMG from the actual root directory, not a hardcoded file slot.
$stream=[IO.File]::OpenRead($scratch)
try {
    $root=New-Object byte[] 512
    $dataStart=[long](2048+32+4096)*512
    $stream.Position=$dataStart
    if ($stream.Read($root,0,512) -ne 512) { throw 'Missing root directory' }
    $kernelOffset=0L
    for ($at=0;$at -lt 512;$at+=32) {
        if ([Text.Encoding]::ASCII.GetString($root,$at,11) -eq 'OUROS   IMG') {
            $cluster=([uint32][BitConverter]::ToUInt16($root,$at+20) -shl 16) -bor [BitConverter]::ToUInt16($root,$at+26)
            $kernelOffset=$dataStart+([long]$cluster-2)*512
        }
    }
    if (!$kernelOffset) { throw 'Kernel entry missing' }
} finally { $stream.Dispose() }
$cases=@(
    @{name='MBR signature';offset=510L;message='Invalid MBR'},
    @{name='FAT mirror';offset=([long](2048+32+2048)*512+12);message='FAT mirror mismatch'},
    @{name='Kernel payload';offset=($kernelOffset+100);message='File mismatch: OUROS.IMG'}
)
$imageManifest=Get-Content -Raw -LiteralPath "$source.json" | ConvertFrom-Json
if ($imageManifest.PSObject.Properties['data_format']) {
    $cases+=@(
        @{name='Data partition extent';offset=474L;message='Data partition extent mismatch'},
        @{name='Data snapshot payload';offset=([long]$imageManifest.data_lba*512+1024);message='Data partition hash mismatch'}
    )
}
foreach ($case in $cases) {
    $stream=[IO.File]::Open($scratch,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        $stream.Position=$case.offset; $original=$stream.ReadByte()
        $stream.Position=$case.offset; $stream.WriteByte([byte]($original -bxor 1))
    } finally { $stream.Dispose() }
    try {
        $rejected=$false
        try { & "$PSScriptRoot/verify-rpi5-image.ps1" -Image $scratch -Manifest "$source.json" }
        catch {
            if (!$_.Exception.Message.Contains($case.message)) { throw }
            $rejected=$true
        }
        if (!$rejected) { throw "Corruption was accepted: $($case.name)" }
        Write-Output "PASS: rejected corrupted $($case.name)"
    } finally {
        $stream=[IO.File]::Open($scratch,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $stream.Position=$case.offset; $stream.WriteByte([byte]$original) }
        finally { $stream.Dispose() }
    }
}
if ((Get-FileHash $scratch).Hash -ne (Get-FileHash $source).Hash) { throw 'Test copy failed to restore' }
$wrongManifest=Get-Content -Raw -LiteralPath "$source.json" | ConvertFrom-Json
$wrongConsole=if ($Console -eq 'header') { 'debug' } else { 'header' }
$wrongManifest | Add-Member -NotePropertyName console -NotePropertyValue $wrongConsole -Force
$wrongPath=Join-Path $work 'wrong-console.json'
$wrongManifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding ASCII $wrongPath
$rejected=$false
try { & "$PSScriptRoot/verify-rpi5-image.ps1" -Image $scratch -Manifest $wrongPath }
catch {
    if (!$_.Exception.Message.Contains('Boot configuration mismatch')) { throw }
    $rejected=$true
}
if (!$rejected) { throw 'Wrong console profile was accepted' }
Write-Output 'PASS: rejected mismatched console profile'
Write-Output 'PASS: original delivered image unchanged; corruption test copy restored.'
