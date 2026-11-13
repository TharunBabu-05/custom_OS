[CmdletBinding()]
param([string]$Image='image_files/OurOS-Pi5-header-experimental-sd.img')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/test-apps-qemu.ps1" -Image $Image -Tools
Write-Output 'PASS: user tools, quoted arguments, private cwd/metadata, sleeping processes and all-asleep resume on both CPU profiles.'
