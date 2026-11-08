[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Image,
      [Parameter(Mandatory=$true)][string]$Manifest)
. "$PSScriptRoot/windows-tools.ps1"
$expected=Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
. "$PSScriptRoot/rpi5-profile.ps1"
$console=if ($expected.PSObject.Properties['console']) { $expected.console } else { 'debug' }
$piProfile=Get-Rpi5Profile $console
$path=(Resolve-Path -LiteralPath $Image).Path
$stream=[IO.File]::OpenRead($path)
function Require([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
try {
    function ReadAt([long]$Offset,[int]$Count) {
        $data=New-Object byte[] $Count
        $stream.Position=$Offset
        $done=0
        while ($done -lt $Count) {
            $read=$stream.Read($data,$done,$Count-$done)
            if (!$read) { throw 'Truncated image' }
            $done+=$read
        }
        return ,$data
    }
    function U16([byte[]]$Data,[int]$Offset) { [BitConverter]::ToUInt16($Data,$Offset) }
    function U32([byte[]]$Data,[int]$Offset) { [BitConverter]::ToUInt32($Data,$Offset) }
    function Same([byte[]]$A,[byte[]]$B) { [Convert]::ToBase64String($A) -ceq [Convert]::ToBase64String($B) }
    Require ($stream.Length -eq 134217728 -and $expected.bytes -eq $stream.Length) 'Unexpected image size'
    $mbr=ReadAt 0 512
    Require ($mbr[510] -eq 85 -and $mbr[511] -eq 170 -and $mbr[450] -eq 12) 'Invalid MBR/FAT32 partition type'
    $start=U32 $mbr 454; $total=U32 $mbr 458
    Require ($start -eq 2048 -and $total -eq 260096 -and ($start+$total)*512 -eq $stream.Length) 'Partition extent mismatch'
    Require ((U32 $mbr 470) -eq 0 -and (U32 $mbr 486) -eq 0 -and (U32 $mbr 502) -eq 0) 'Unexpected extra partitions'
    $boot=ReadAt ([long]$start*512) 512
    $reserved=U16 $boot 14; $fatSectors=U32 $boot 36
    Require ((U16 $boot 11) -eq 512 -and $boot[13] -eq 1 -and $boot[16] -eq 2 -and
        $reserved -eq 32 -and $fatSectors -eq 2048 -and (U32 $boot 32) -eq $total -and
        (U32 $boot 28) -eq $start -and (U32 $boot 44) -eq 2 -and
        (U16 $boot 17) -eq 0 -and (U16 $boot 22) -eq 0 -and
        (U16 $boot 48) -eq 1 -and (U16 $boot 50) -eq 6 -and
        $boot[510] -eq 85 -and $boot[511] -eq 170) 'Invalid FAT32 BPB'
    $clusters=$total-$reserved-2*$fatSectors
    Require ($clusters -ge 65525 -and ($clusters+2)*4 -le $fatSectors*512) 'Not FAT32 by cluster count'
    Require (Same $boot (ReadAt (($start+6)*512) 512)) 'Backup boot sector mismatch'
    $info=ReadAt (($start+1)*512) 512
    Require (Same $info (ReadAt (($start+7)*512) 512)) 'Backup FSInfo mismatch'
    Require ((U32 $info 0) -eq 0x41615252 -and (U32 $info 484) -eq 0x61417272 -and
        $info[510] -eq 85 -and $info[511] -eq 170) 'Invalid FSInfo signatures'
    $fat=ReadAt (($start+$reserved)*512) ($fatSectors*512)
    Require (Same $fat (ReadAt (($start+$reserved+$fatSectors)*512) ($fatSectors*512))) 'FAT mirror mismatch'
    Require ((U32 $fat 0) -eq 0x0ffffff8 -and (U32 $fat 4) -eq 0x0fffffff -and
        (U32 $fat 8) -eq 0x0fffffff) 'Reserved/root FAT entries invalid'
    $free=0
    for ($cluster=2;$cluster -lt $clusters+2;$cluster++) { if ([BitConverter]::ToUInt32($fat,$cluster*4) -eq 0) { $free++ } }
    Require ($free -eq (U32 $info 488)) 'FSInfo free count mismatch'
    $next=U32 $info 492
    Require ($next -ge 2 -and $next -lt $clusters+2 -and (U32 $fat ($next*4)) -eq 0) 'Invalid next-free hint'
} finally { $stream.Dispose() }

# Independent reader: 7-Zip decodes the MBR, then the FAT32 filesystem and chains.
$seven=Join-Path $ProjectRoot '.dist/7zip/7z.exe'
$work=Join-Path $ProjectRoot ('build/rpi5/image-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
# 7-Zip automatically descends through a single MBR partition into its FAT.
& $seven t $path | Set-Content (Join-Path $work 'fat-test.log')
if ($LASTEXITCODE -ne 0) { throw 'Independent FAT test failed' }
& $seven x $path "-o$work/files" -y | Set-Content (Join-Path $work 'fat-extract.log')
if ($LASTEXITCODE -ne 0) { throw 'Independent FAT extraction failed' }
$actual=@(Get-ChildItem -LiteralPath (Join-Path $work 'files') -File)
Require ($actual.Count -eq $expected.files.Count) 'Unexpected FAT file count'
foreach ($file in $expected.files) {
    Require ($file.name -cmatch '^[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}$') 'Invalid manifest name'
    $entry=Join-Path $work "files/$($file.name)"
    Require ((Get-Item -LiteralPath $entry).Length -eq $file.bytes -and
        (Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash -eq $file.sha256) "File mismatch: $($file.name)"
}
$config=Get-Content -Raw -LiteralPath (Join-Path $work 'files/CONFIG.TXT')
$reviewed=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot $piProfile.Config)
$reviewed=$reviewed.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
Require ($config -ceq $reviewed) 'Boot configuration mismatch'
$assets=Get-Content -Raw -LiteralPath (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
Require ((Get-FileHash (Join-Path $work 'files/PI5.DTB')).Hash -eq $assets.files[0].sha256) 'DTB pin mismatch'
$kernel=[IO.File]::ReadAllBytes((Join-Path $work 'files/OUROS.IMG'))
$isHeader=[Text.Encoding]::ASCII.GetString($kernel).Contains("rpi5-header`0")
Require ($isHeader -eq ($console -eq 'header')) 'Kernel/console profile mismatch'
Require ($kernel.Length -ge 64 -and [BitConverter]::ToUInt32($kernel,56) -eq 0x644d5241 -and
    [BitConverter]::ToUInt64($kernel,8) -eq 0x80000 -and
    [BitConverter]::ToUInt64($kernel,16) -ge $kernel.Length -and
    [BitConverter]::ToUInt64($kernel,16) -lt 0x180000) 'Kernel Image header invalid'
Write-Output 'PASS: MBR, FAT32 geometry/backups/mirrors/free space, independent extraction, all file hashes and boot configuration.'
