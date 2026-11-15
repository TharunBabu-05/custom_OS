# Application-only delivery for the hardware-tested private-page kernel.
# Creates regular workspace files; never opens or modifies a physical drive.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/disk-image-backups.ps1"
$project=Split-Path $PSScriptRoot -Parent
$apps=Get-ArtifactPath (Join-Path $project 'build/apps') $project
$kernel=Get-ArtifactPath (Join-Path $project 'build/rpi5-header/ouros-rpi5.img') $project
$requiredKernel='72852aa77b033b9f6330b0f93a8f9dbcbff8d9c864568db792d51d3946813065'
if ((Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash.ToLowerInvariant() -cne $requiredKernel) {
    throw 'Kernel differs from the Pi MEMCHK stage. Review application-only compatibility before packaging.'
}
$work=Get-ArtifactPath (Join-Path $apps ('archive-check-'+[guid]::NewGuid().ToString('N'))) $apps
$files=Join-Path $work 'files';New-Item -ItemType Directory -Path $files | Out-Null
$entries=@()
foreach ($name in @('HEAPCHK.ELF','HEAPDEMO.ELF')) {
    $source=Get-ArtifactPath (Join-Path $apps $name) $apps
    [byte[]]$payload=[IO.File]::ReadAllBytes($source)
    if ($payload.Length -lt 64 -or $payload[0] -ne 127 -or [Text.Encoding]::ASCII.GetString($payload,1,3) -cne 'ELF' -or
        $payload[4] -ne 2 -or $payload[5] -ne 1 -or [BitConverter]::ToUInt16($payload,16) -ne 2 -or
        [BitConverter]::ToUInt16($payload,18) -ne 183) { throw "Not an AArch64 ELF executable: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $files $name)
    $entries+=@{name=$name;bytes=$payload.Length;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$readme=@'
OurOS application byte allocator - application-only update

These two static executables contain the complete new user-space allocator.
No kernel, configuration or partition change is needed for the MEMCHK-stage
OurOS Pi 5 image. The required kernel SHA256 is recorded in manifest.json.
The matched kernel supports ALLOC_PAGES/FREE_PAGES/MEMORY_INFO (calls 37-39).

1. Shut down/power off the Pi. Connect its existing pendrive to Windows.
2. Extract this ZIP to a folder on the PC.
3. Open the pendrive's existing FAT boot volume containing CONFIG.TXT and
   OUROS.IMG. Copy HEAPCHK.ELF and HEAPDEMO.ELF into that volume's root.
   Do not format/reflash the drive or copy files onto the data partition.
4. Safely eject the drive, return it to the Pi and boot normally.
5. At the kernel monitor, enter these commands one per line:
   storageinit
   mmuinit
   irqinit
   timerstart
   appterm 120 /boot/HEAPCHK.ELF
   appterm 120 /boot/HEAPDEMO.ELF

HEAPCHK should print PASS and exit code=0 with cleanup=restored.
HEAPDEMO should end with "Released buffers; private_pages=0" and code=0.
datainit is optional for these tests; they do not write files.
In appshell, "run heapdemo" and "run heapchk" also work.
Only copy the two ELFs. This preserves the existing kernel and /data contents.

The byte allocator was tested on native models and QEMU. These new
applications still require a Pi hardware run. They do not provide a general
libc, package manager, background services or a desktop interface.
'@
[IO.File]::WriteAllText((Join-Path $files 'README.txt'),$readme,[Text.Encoding]::ASCII)
$manifest=[ordered]@{format='OurOS application-only bundle v1';stage='user-byte-allocator';architecture='AArch64';
    required_kernel_sha256=$requiredKernel;required_syscalls=@(37,38,39);files=$entries;physical_byte_allocator_acceptance='pending'}
[IO.File]::WriteAllText((Join-Path $files 'manifest.json'),($manifest | ConvertTo-Json -Depth 5),[Text.Encoding]::ASCII)
$hashLines=@($entries | ForEach-Object { $_.sha256+'  '+$_.name })
[IO.File]::WriteAllText((Join-Path $files 'SHA256.txt'),(($hashLines -join "`r`n")+"`r`n"),[Text.Encoding]::ASCII)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary=Join-Path $work 'bundle.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($files,$temporary,[IO.Compression.CompressionLevel]::Optimal,$false)
$zip=[IO.Compression.ZipFile]::OpenRead($temporary)
try {
    $actual=@($zip.Entries | ForEach-Object { $_.FullName } | Sort-Object)
    $expected=@('HEAPCHK.ELF','HEAPDEMO.ELF','README.txt','SHA256.txt','manifest.json' | Sort-Object)
    if (($actual -join '|') -cne ($expected -join '|')) { throw 'Unexpected ZIP contents' }
} finally { $zip.Dispose() }
$expanded=Join-Path $work 'expanded'
[IO.Compression.ZipFile]::ExtractToDirectory($temporary,$expanded)
foreach ($name in $expected) {
    if ((Get-FileHash -LiteralPath (Join-Path $files $name) -Algorithm SHA256).Hash -cne
        (Get-FileHash -LiteralPath (Join-Path $expanded $name) -Algorithm SHA256).Hash) { throw "ZIP extraction hash differs: $name" }
}
$destination=Get-ArtifactPath (Join-Path $apps 'OurOS-byte-allocator-apps.zip') $apps
Move-Item -LiteralPath $temporary -Destination $destination -Force
Write-Output 'PASS: application-only ZIP exact entries and all extracted hashes; kernel identical to the hardware-tested MEMCHK stage.'
Write-Output "APPS ZIP: $destination"
Write-Output ('ZIP SHA256: '+(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant())
Write-Output 'Copy only the two ELFs to the existing boot volume; no reflashing or /data change required.'
