# Writes a regular project-local disk-image file; never opens a physical device.
[CmdletBinding()]
param([ValidateSet('debug','header')][string]$Console = 'debug')
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
$piProfile=Get-Rpi5Profile $Console
& "$PSScriptRoot/package-rpi5.ps1" -Console $Console
$out=Join-Path $ProjectRoot $piProfile.Output
$work=Join-Path $out ('disk-stage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$bundle=Join-Path $work 'bundle'
Expand-Archive -LiteralPath (Join-Path $out 'ouros-rpi5-boot-files.zip') -DestinationPath $bundle
& "$PSScriptRoot/verify-rpi5-package.ps1" -Directory $bundle
$files=Join-Path $work 'files'
New-Item -ItemType Directory -Path $files | Out-Null
& "$PSScriptRoot/build-apps.ps1"
Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'build/apps') -Filter '*.ELF' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $files $_.Name)
}
# Explicit 8.3 names avoid dependence on a custom long-filename implementation.
$map=[ordered]@{'OUROS.IMG'='ouros-rpi5.img';'PI5.DTB'='bcm2712-rpi-5-b.dtb';
    'COPYING.TXT'='COPYING.linux';'LICENSE.TXT'='LICENCE.broadcom'}
foreach ($name in $map.Keys) { Copy-Item -LiteralPath (Join-Path $bundle $map[$name]) -Destination (Join-Path $files $name) }
. "$PSScriptRoot/wifi-assets.ps1"
foreach ($asset in $WifiAssets.files) { Copy-Item -LiteralPath (Join-Path $WifiCache $asset.name) -Destination (Join-Path $files $asset.name) }
$config=Get-Content -Raw -LiteralPath (Join-Path $bundle 'config.txt')
$config=$config.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
[IO.File]::WriteAllText((Join-Path $files 'CONFIG.TXT'),$config,[Text.Encoding]::ASCII)
$readme=@'
OurOS experimental Raspberry Pi 5 UART image
Original AArch64 kernel and user runtime; early OS, not a complete desktop.
Pi verifies ELF/EL0/preemption, local installation, shell and copy/hash/sync.
Pi also verifies spawn/wait, early terminal handoff and child timeout cleanup.
New file standard-input/output redirection needs Pi acceptance.

Console: 3-pin UART connector between the HDMI ports. Terminal 115200 8N1.
Only the dedicated UART10 console is used. No flow control.
No HDMI display, USB keyboard/mouse, GPU or audio driver is included.

Earlier Pi logs verify page/heap allocation, MMU protection, exception/IRQ
return, GICv2 timer, cooperative task/file I/O and USB mount/write/read/sync.
WPA3/DHCP/DNS/Internet ping have earlier physical evidence. Networking is an
explicit foreground console; no background renewal/rekey or downloads.

CURRENT APPLICATION TEST: one command per line; wait for each prompt:
storageinit, datainit, mmuinit, irqinit, timerstart, apptest, tooltest, appinfo,
taskiotest, fileinfo, heaptest, pagetest, timerstop.
apptest runs original static ELF programs with private EL0 pages, checked
syscalls and timer preemption. It contains deliberate null/kernel-access,
RX-write and NX-stack faults and times out a runaway user loop.
Expect final PASS, cleanup=restored, zero handles, responsive monitor and
passing task/memory checks. Only user frames switch; kernel drivers remain
synchronous. Batch budget 2 seconds; image max32KiB, guarded stack16KiB.
INTERACTIVE SHELL: after MMU/timer/storage setup, run appshell.
EL0 prompt user:/$ supports help pwd cd ls cat echo write mkdir rm rmdir exit.
write /tmp/note "hello from EL0" creates or replaces the whole file.
Ctrl+C cancels; Ctrl+D at an empty line exits; Backspace/Delete and Ctrl+U edit.
127-byte lines, 300-second maximum session. Kernel monitor returns on exit.
cp <source> <new-target> copies up to 4096 bytes; existing targets refused.
sha256 <file> checks up to 64 KiB; sync flushes mounted /data.
run <ELF> [args] launches a private child and waits, with a 10-second limit.
status shows its last exit code; Ctrl+C stops the child and returns to the shell.
runio <input|-> <new-output|-> <ELF> [args] redirects file streams.
Output refuses existing files, keeps binary bytes and has a 4096-byte limit.
CAT.ELF with no path reads redirected stdin. Use appterm 30 /boot/IOCHK.ELF.
No pipes, background jobs, environment or permissions yet.
See docs/stdio.md. User tools: appexec /boot/LS.ELF /boot; appexec /boot/CAT.ELF /etc/hostname
appexec /boot/ECHO.ELF "hello from EL0"; appexec /boot/SLEEP.ELF 100
Arguments and cwd are private; sleep yields the CPU, including idle resume.
See docs/user-tools.md and docs/remaining-os-roadmap.md in the source.

LOCAL INSTALLATION (MMU and timer running):
appinstall /boot/HELLO.ELF /data/hello.elf
apprun /data/hello.elf
Installer accepts a validated ELF <=4096 bytes, creates a NEW flat data file,
commits synchronously and verifies full readback. Existing targets refused.
This is local executable installation, not an OS installer or package manager.
Power-cycle WITHOUT reflashing, then storageinit, datainit, mmuinit, irqinit,
timerstart, apprun /data/hello.elf, appinfo, timerstop (separate commands).

STORAGE: root / is volatile RAMFS. /boot is read-only FAT32. /data has eight
flat 4 KiB files, two SHA256 snapshots in its own 1 MiB partition. Every update
commits synchronously. Reflashing replaces /data. Physical power-cycle
persistence still needs verification. Run storageinit BEFORE usbtest/usbenum.
On failed updates preserve datainfo/storageinfo and reboot before retrying.
No guest format/raw-device/EEPROM-update command exists.

FILES: pwd ls cd cat mkdir touch write rm rmdir fsinfo; filetest fileinfo.
fopen <path> r|rw|w returns a generated handle; fread <handle> <1..512>;
fwrite <handle> <text>; fseek <handle> <offset> set|cur|end; fclose <handle>.
Open files pin paths; conflicting edits report busy until handles close.

BOOT ASSETS: OUROS.IMG kernel, *.ELF original user programs, PI5.DTB official
board DTB renamed only, CONFIG.TXT configuration. COPYING.TXT/LICENSE.TXT/
WIFILIC.TXT retain notices; SOURCE.TXT pinned provenance. No Linux kernel.
Building creates regular project files, flashes no physical drive and pushes
nothing to GitHub. Filesystem validation does not establish hardware boot.
'@
if ($Console -eq 'header') {
    $readme=$readme.Replace('Only the dedicated UART10 console is used.', 'Only the RP1 UART0 header console is used.')
    $readme=$readme.Replace('3-pin UART connector between the HDMI ports.', '40-pin header: Pi pin 8 TX -> adapter RXD, pin 10 RX <- adapter TXD, pin 6 GND.')
    $readme+="`r`nLeave adapter power pins disconnected; use 3.3 V signals and Pi USB-C power.`r`nRequires firmware enable_rp1_uart=1, pciex4_reset=0; board=rpi5-header.`r`nSee docs/rpi5-header-uart.md. No PCIe reinitialization is implemented.`r`n"
    $readme+="UART base 0x1c00030000 matches captured RP1_UART on board revision d04171.`r`nOther firmware mappings require a matching build. Pi verifies file/task I/O, USB data writes and EL0 execution; new interactive terminal/shell requires its own run.`r`n"
}
[IO.File]::WriteAllText((Join-Path $files 'README.TXT'),$readme,[Text.Encoding]::ASCII)
$assets=Get-Content -Raw (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
$source="Official DTB and notices: $($assets.repository)/tree/$($assets.revision)/boot`r`nWi-Fi vendor assets: $($WifiAssets.repository)/tree/$($WifiAssets.revision)/debian/config/brcm80211`r`nRetain WIFILIC.TXT. FW43455.BIN is the standard Cypress radio firmware, not an OS kernel.`r`nKernel: original OurOS local source, no Linux kernel linked.`r`nDTB redistribution: retain GPL notices and provide corresponding source before public distribution.`r`nOriginal OurOS source license remains undecided. This is a local test artifact.`r`n"
[IO.File]::WriteAllText((Join-Path $files 'SOURCE.TXT'),$source,[Text.Encoding]::ASCII)
$entries=@(Get-ChildItem -LiteralPath $files -File | Sort-Object Name)
# One volume label and one explicit end marker in addition to each file.
$rootClusters=[int][Math]::Ceiling(($entries.Count+2)/16.0)
if ($rootClusters -lt 1 -or $rootClusters -gt 4) { throw 'Root directory capacity exceeded' }

# Fixed 128 MiB disk, 1 MiB partition alignment, FAT32 with 512-byte clusters.
$sector=512; $diskSectors=262144; $start=2048; $dataSectors=2048; $dataStart=$diskSectors-$dataSectors; $total=$dataStart-$start
$reserved=32; $fatSectors=2048; $clusters=$total-$reserved-2*$fatSectors
if ($clusters -lt 65525 -or ($clusters+2)*4 -gt $fatSectors*$sector) { throw 'Invalid FAT32 geometry' }
$dataLba=$start+$reserved+2*$fatSectors
$image=Join-Path $out $piProfile.Image
$temporary=Join-Path $work 'disk.img'
function Put16([byte[]]$Buffer,[int]$Offset,[uint16]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer,$Offset) }
function Put32([byte[]]$Buffer,[int]$Offset,[uint32]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer,$Offset) }
function PutText([byte[]]$Buffer,[int]$Offset,[string]$Value) { [Text.Encoding]::ASCII.GetBytes($Value).CopyTo($Buffer,$Offset) }
$stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $stream.SetLength([long]$diskSectors*$sector)
    function WriteAt([long]$Offset,[byte[]]$Bytes) {
        $stream.Position=$Offset
        $stream.Write($Bytes,0,$Bytes.Length)
    }
    $mbr=New-Object byte[] 512
    Put32 $mbr 440 0x4f55524f
    $mbr[446]=0x80; $mbr[447]=0x20; $mbr[448]=0x21; $mbr[449]=0
    $mbr[450]=0x0c; $mbr[451]=0xfe; $mbr[452]=0xff; $mbr[453]=0xff
    Put32 $mbr 454 $start; Put32 $mbr 458 $total
    $mbr[466]=0xda; $mbr[467]=0xfe; $mbr[468]=0xff; $mbr[469]=0xff
    Put32 $mbr 470 $dataStart; Put32 $mbr 474 $dataSectors
    $mbr[510]=0x55; $mbr[511]=0xaa
    WriteAt 0 $mbr
    $boot=New-Object byte[] 512
    $boot[0]=0xeb; $boot[1]=0x58; $boot[2]=0x90
    PutText $boot 3 'OUR0S   '
    Put16 $boot 11 512; $boot[13]=1; Put16 $boot 14 $reserved; $boot[16]=2
    $boot[21]=0xf8; Put16 $boot 24 63; Put16 $boot 26 255
    Put32 $boot 28 $start; Put32 $boot 32 $total; Put32 $boot 36 $fatSectors
    Put32 $boot 44 2; Put16 $boot 48 1; Put16 $boot 50 6
    $boot[64]=0x80; $boot[66]=0x29; Put32 $boot 67 0x4f555235
    PutText $boot 71 'OUROSBOOT  '; PutText $boot 82 'FAT32   '
    $boot[90]=0xeb; $boot[91]=0xfe # x86 loop, irrelevant to Pi firmware.
    $boot[510]=0x55; $boot[511]=0xaa
    WriteAt ($start*$sector) $boot
    WriteAt (($start+6)*$sector) $boot
    $fat=New-Object byte[] ($fatSectors*$sector)
    Put32 $fat 0 0x0ffffff8; Put32 $fat 4 0x0fffffff
    for ($i=0;$i -lt $rootClusters;$i++) {
        $value=if ($i -eq $rootClusters-1) { 0x0fffffff } else { 3+$i }
        Put32 $fat ((2+$i)*4) $value
    }
    $directory=New-Object byte[] ($rootClusters*512)
    PutText $directory 0 'OUROSBOOT  '; $directory[11]=8
    $nextCluster=2+$rootClusters; $slot=1
    $records=@()
    foreach ($entry in $entries) {
        if ($entry.Name -cnotmatch '^[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}$' -or $entry.Length -eq 0) { throw "Unsupported file: $($entry.Name)" }
        $contents=[IO.File]::ReadAllBytes($entry.FullName)
        $count=[int][Math]::Ceiling($contents.Length/512.0)
        if ($nextCluster+$count -gt $clusters+2) { throw 'Image full' }
        for ($i=0;$i -lt $count;$i++) {
            $value=if ($i -eq $count-1) { 0x0fffffff } else { $nextCluster+$i+1 }
            Put32 $fat (($nextCluster+$i)*4) $value
        }
        WriteAt (([long]$dataLba+$nextCluster-2)*$sector) $contents
        $offset=$slot*32; $parts=$entry.Name.Split('.')
        PutText $directory $offset ($parts[0].PadRight(8)+$parts[1].PadRight(3))
        $directory[$offset+11]=0x20
        # Deterministic FAT date: 2026-11-08, 00:00. No host-time dependency.
        Put16 $directory ($offset+16) 23912; Put16 $directory ($offset+18) 23912
        Put16 $directory ($offset+24) 23912
        Put16 $directory ($offset+20) ([uint16]($nextCluster -shr 16))
        Put16 $directory ($offset+26) ([uint16]($nextCluster -band 65535))
        Put32 $directory ($offset+28) $contents.Length
        $records+=[ordered]@{name=$entry.Name;bytes=$contents.Length;sha256=(Get-FileHash $entry.FullName -Algorithm SHA256).Hash}
        $nextCluster+=$count; $slot++
    }
    WriteAt ([long]$dataLba*$sector) $directory
    WriteAt (($start+$reserved)*$sector) $fat
    WriteAt (($start+$reserved+$fatSectors)*$sector) $fat
    $fsinfo=New-Object byte[] 512
    Put32 $fsinfo 0 0x41615252; Put32 $fsinfo 484 0x61417272
    Put32 $fsinfo 488 ($clusters-($nextCluster-2)); Put32 $fsinfo 492 $nextCluster
    $fsinfo[510]=0x55; $fsinfo[511]=0xaa
    WriteAt (($start+1)*$sector) $fsinfo
    WriteAt (($start+7)*$sector) $fsinfo
    $extra=New-Object byte[] 512; $extra[510]=0x55; $extra[511]=0xaa
    WriteAt (($start+2)*$sector) $extra; WriteAt (($start+8)*$sector) $extra
    . "$PSScriptRoot/data-format.ps1"
    $dataPartition=New-OurOSDataPartition
    WriteAt ([long]$dataStart*$sector) $dataPartition
    $dataHasher=[Security.Cryptography.SHA256]::Create()
    try { $dataDigest=([BitConverter]::ToString($dataHasher.ComputeHash($dataPartition))).Replace('-','').ToLowerInvariant() } finally { $dataHasher.Dispose() }
    $stream.Flush()
} finally { $stream.Dispose() }
$manifest=[ordered]@{board='Raspberry Pi 5 Model B';status='EXPERIMENTAL-HARDWARE-UNTESTED';
    bytes=134217728;partition_lba=$start;partition_sectors=$total;fat='FAT32';
    asset_revision=$assets.revision;wifi_revision=$WifiAssets.revision;console=$Console;files=$records;
    data_format=1;data_lba=$dataStart;data_sectors=$dataSectors;data_sha256=$dataDigest}
$manifestPath=Join-Path $work 'image-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding ASCII $manifestPath
& "$PSScriptRoot/verify-rpi5-image.ps1" -Image $temporary -Manifest $manifestPath
# Promote only a verified artifact. Output paths are fixed project-local files.
if (Test-Path -LiteralPath $image) { & "$PSScriptRoot/archive-images.ps1" -Image $image }
Copy-Item -LiteralPath $temporary -Destination $image -Force
Copy-Item -LiteralPath $manifestPath -Destination "$image.json" -Force
$digest=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $([IO.Path]::GetFileName($image))" | Set-Content -Encoding ASCII "$image.sha256"
& "$PSScriptRoot/archive-images.ps1" -Image $image
& "$PSScriptRoot/compress-image.ps1" -Image $image
& "$PSScriptRoot/clean-build.ps1" -IncludeTestImages
Write-Output "IMAGE: $image"
Write-Output "SHA256: $digest"
Write-Output 'Experimental Pi 5 image. Filesystem verified; hardware boot UNTESTED. No physical device written.'
