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
# Explicit 8.3 names avoid dependence on a custom long-filename implementation.
$map=[ordered]@{'OUROS.IMG'='ouros-rpi5.img';'PI5.DTB'='bcm2712-rpi-5-b.dtb';
    'COPYING.TXT'='COPYING.linux';'LICENSE.TXT'='LICENCE.broadcom'}
foreach ($name in $map.Keys) { Copy-Item -LiteralPath (Join-Path $bundle $map[$name]) -Destination (Join-Path $files $name) }
$config=Get-Content -Raw -LiteralPath (Join-Path $bundle 'config.txt')
$config=$config.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
[IO.File]::WriteAllText((Join-Path $files 'CONFIG.TXT'),$config,[Text.Encoding]::ASCII)
$readme=@'
OurOS experimental Raspberry Pi 5 UART boot image
HARDWARE BOOT UNTESTED. This is an early kernel, not a complete operating system.
No GUI/HDMI, USB keyboard, multitasking, virtual memory, storage writes, networking
or user-space applications. Boot firmware reads FAT; the kernel creates a
separate volatile RAMFS root. Run storageinit to attempt a read-only FAT32 mount.
Only the dedicated UART10 console is used.

Requires Pi 5, working EEPROM firmware and a 3.3 V serial adapter/cable at the
3-pin UART connector between the HDMI ports. Terminal: 115200 8N1, no flow control.
Expected message: OurOS: AArch64 kernel booted
Commands: help info cpuinfo meminfo memmap uptime echo clear halt fault
Memory commands: pagealloc, pagefree <slot>, pagetest.
Earlier DTB memory discovery and 4 KiB pages have Pi hardware evidence.
MB/MiB summaries and RAMFS commands now have physical Pi evidence.
New storage/SDIO paths require a new physical capture; host/QEMU checks passed.
Dynamic reserved-memory alloc-ranges windows are conservatively excluded in full.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
Files disappear after reboot. See docs/ramfs.md. Seed files own 3 physical pages.
Storage: storageinfo, storageinit, ls /boot, cat /boot/CONFIG.TXT.
Pi driver inherits an active SDHC/SDXC card from firmware; no full init yet.
Firmware USB boot works on the user's header build with a 32 GB pendrive.
The retained -sd.img filename does not limit the disk image to SD media.
storageinit cannot mount USB: kernel USB disk I/O remains pending.
usbinfo: DT USB hosts. usbprobe: read-only xHCI controller/port observation.
USB MMIO probe requires the header UART's known firmware RP1 window.
These USB commands are a new hardware-untested milestone; no enumeration yet.
/boot is read-only; no card write commands exist in this revision.
Wi-Fi: wifiinfo, wifiprobe (diagnostics only; no scan/connect/IP).
Root dirs: /boot /dev /etc /home /mnt /proc /sys /tmp /var/log.
See docs/storage.md, docs/usb.md and docs/wifi.md for limits and test instructions.

OUROS.IMG is the kernel. PI5.DTB is the official board device tree (renamed only).
CONFIG.TXT selects those filenames. COPYING.TXT and LICENSE.TXT retain upstream
notices. SOURCE.TXT records provenance. No Linux kernel or EEPROM update is here.

See docs/rpi5-boot.md and docs/disk-image.md in the source repository.
Do not infer hardware success from filesystem validation. Record a UART boot log.
'@
if ($Console -eq 'header') {
    $readme=$readme.Replace('Only the dedicated UART10 console is used.', 'Only the RP1 UART0 header console is used.')
    $readme=$readme.Replace('3-pin UART connector between the HDMI ports.', '40-pin header: Pi pin 8 TX -> adapter RXD, pin 10 RX <- adapter TXD, pin 6 GND.')
    $readme+="`r`nLeave adapter power pins disconnected; use 3.3 V signals and Pi USB-C power.`r`nRequires firmware enable_rp1_uart=1, pciex4_reset=0; board=rpi5-header.`r`nSee docs/rpi5-header-uart.md. No PCIe reinitialization is implemented.`r`n"
    $readme+="UART base 0x1c00030000 matches captured RP1_UART on board revision d04171.`r`nOther firmware mappings require a matching build; physical kernel boot remains unverified.`r`n"
}
[IO.File]::WriteAllText((Join-Path $files 'README.TXT'),$readme,[Text.Encoding]::ASCII)
$assets=Get-Content -Raw (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
$source="Official DTB and notices: $($assets.repository)/tree/$($assets.revision)/boot`r`nKernel: original OurOS local source, no Linux kernel linked.`r`nDTB redistribution: retain GPL notices and provide corresponding source before public distribution.`r`nOriginal OurOS source license remains undecided. This is a local test artifact.`r`n"
[IO.File]::WriteAllText((Join-Path $files 'SOURCE.TXT'),$source,[Text.Encoding]::ASCII)
$entries=@(Get-ChildItem -LiteralPath $files -File | Sort-Object Name)
if ($entries.Count -gt 14) { throw 'Root directory capacity exceeded' }

# Fixed 128 MiB disk, 1 MiB partition alignment, FAT32 with 512-byte clusters.
$sector=512; $diskSectors=262144; $start=2048; $total=$diskSectors-$start
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
    Put32 $fat 0 0x0ffffff8; Put32 $fat 4 0x0fffffff; Put32 $fat 8 0x0fffffff
    $directory=New-Object byte[] 512
    PutText $directory 0 'OUROSBOOT  '; $directory[11]=8
    $nextCluster=3; $slot=1
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
    $stream.Flush()
} finally { $stream.Dispose() }
$manifest=[ordered]@{board='Raspberry Pi 5 Model B';status='EXPERIMENTAL-HARDWARE-UNTESTED';
    bytes=134217728;partition_lba=$start;partition_sectors=$total;fat='FAT32';
    asset_revision=$assets.revision;console=$Console;files=$records}
$manifestPath=Join-Path $work 'image-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding ASCII $manifestPath
& "$PSScriptRoot/verify-rpi5-image.ps1" -Image $temporary -Manifest $manifestPath
# Promote only a verified artifact. Output paths are fixed project-local files.
Copy-Item -LiteralPath $temporary -Destination $image -Force
Copy-Item -LiteralPath $manifestPath -Destination "$image.json" -Force
$digest=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $([IO.Path]::GetFileName($image))" | Set-Content -Encoding ASCII "$image.sha256"
Write-Output "IMAGE: $image"
Write-Output "SHA256: $digest"
Write-Output 'Experimental Pi 5 image. Filesystem verified; hardware boot UNTESTED. No physical device written.'
