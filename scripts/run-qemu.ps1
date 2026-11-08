[CmdletBinding()]
param([ValidateSet('cortex-a53','cortex-a76')][string]$Cpu='cortex-a53',
      [ValidateSet(128,512)][int]$MemoryMB=128, [switch]$Gdb,[string]$StorageImage)
. "$PSScriptRoot/windows-tools.ps1"
$kernel=Join-Path $ProjectRoot 'build/qemu_virt/kernel.elf'
if (!(Test-Path -LiteralPath $kernel)) { throw 'Build first: scripts/build.ps1 -Target virt' }
$options=@('-machine','virt-9.2,virtualization=off,secure=off','-cpu',$Cpu,
    '-accel','tcg','-smp','1','-m',"$MemoryMB",'-display','none',
    '-monitor','none','-serial','stdio','-net','none','-kernel',$kernel)
if ($Gdb) { $options+=@('-S','-gdb','tcp:127.0.0.1:1234') }
if ($StorageImage) {
    $disk=(Resolve-Path -LiteralPath $StorageImage).Path
    $options+=@('-global','virtio-mmio.force-legacy=false','-drive',"file=$disk,if=none,format=raw,readonly=on,id=bootdisk",
        '-device','virtio-blk-device,drive=bootdisk')
}
Write-Output 'QEMU virt only. Use Ctrl+C to stop the emulator. halt stops the guest CPU only.'
& $QemuExe @options
if ($LASTEXITCODE -ne 0) { throw "QEMU exited with $LASTEXITCODE" }
