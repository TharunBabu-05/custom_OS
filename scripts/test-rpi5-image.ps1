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
    $kernelOffset=0L
    $rootCluster=2L
    for ($hop=0;$hop -lt 4 -and $rootCluster -lt 0x0ffffff8;$hop++) {
        if ($rootCluster -lt 2 -or $rootCluster -ge 253922) { throw 'Invalid root chain' }
        $stream.Position=$dataStart+($rootCluster-2)*512
        if ($stream.Read($root,0,512) -ne 512) { throw 'Missing root directory' }
        for ($at=0;$at -lt 512;$at+=32) {
            if ([Text.Encoding]::ASCII.GetString($root,$at,11) -eq 'OUROS   IMG') {
                $cluster=([uint32][BitConverter]::ToUInt16($root,$at+20) -shl 16) -bor [BitConverter]::ToUInt16($root,$at+26)
                $kernelOffset=$dataStart+([long]$cluster-2)*512
            }
        }
        $fatWord=New-Object byte[] 4;$stream.Position=[long](2048+32)*512+$rootCluster*4
        if ($stream.Read($fatWord,0,4) -ne 4) { throw 'Missing root link' }
        $rootCluster=[BitConverter]::ToUInt32($fatWord,0) -band 0x0fffffff
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
if ($imageManifest.files.Count -gt 14) {
    $cases+=@(
        @{name='Root chain cycle';offsets=@(([long](2048+32)*512+8),([long](2048+32+2048)*512+8));message='Cyclic root chain'},
        @{name='Root/file crosslink';offset=($dataStart+32+26);xor=6;message='Cross-linked or cyclic file/root cluster'}
    )
}
foreach ($case in $cases) {
    $offsets=if ($case.ContainsKey('offsets')) { @($case.offsets) } else { @($case.offset) }
    $originals=@();$mask=if ($case.ContainsKey('xor')) { $case.xor } else { 1 }
    $stream=[IO.File]::Open($scratch,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        foreach ($offset in $offsets) {
            $stream.Position=$offset;$original=$stream.ReadByte();$originals+=$original
            $stream.Position=$offset;$stream.WriteByte([byte]($original -bxor $mask))
        }
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
        try { for ($i=0;$i -lt $offsets.Count;$i++) { $stream.Position=$offsets[$i];$stream.WriteByte([byte]$originals[$i]) } }
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
