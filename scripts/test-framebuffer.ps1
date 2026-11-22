[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=(New-Item -ItemType Directory -Force build/framebuffer-tests).FullName
    $kernel=(Resolve-Path 'build/handoff-test/kernel.bin').Path
    $plain=Join-Path $out 'plain.dtb'
    $valid=Join-Path $out 'valid.dtb'
    $bad=Join-Path $out 'bad-stride.dtb'
    & $QemuExe -machine "virt-9.2,virtualization=off,secure=off,dumpdtb=$plain" -cpu cortex-a53 -m 128 -display none -monitor none -serial null -net none
    if ($LASTEXITCODE) { throw 'DTB generation failed' }
    & node tests/framebuffer_dtb.mjs $plain $valid valid
    if ($LASTEXITCODE) { throw 'Framebuffer fixture generation failed' }
    & node tests/framebuffer_dtb.mjs $plain $bad bad-stride
    if ($LASTEXITCODE) { throw 'Bad-stride fixture generation failed' }
    $fixture=Join-Path $out 'framebuffer-host.exe'
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/framebuffer_host.c drivers/display/simple_framebuffer.c drivers/display/framebuffer_terminal.c kernel/core/dt_property.c kernel/memory/fdt_memory.c kernel/memory/map.c kernel/memory/mmu_tables.c -o $fixture
    if ($LASTEXITCODE) { throw 'Framebuffer native test build failed' }
    & $fixture $plain $valid $bad
    if ($LASTEXITCODE) { throw 'Framebuffer native tests failed' }

    $width=320
    $height=192
    $stride=1288
    $guard=8
    function Send-Qmp($inputStream,$outputStream,[hashtable]$Command) {
        $outputStream.WriteLine(($Command | ConvertTo-Json -Compress -Depth 4))
        do {
            $line=$inputStream.ReadLine()
            if (!$line) { throw 'QMP disconnected' }
            $reply=$line | ConvertFrom-Json
        } while ($reply.event)
        if ($reply.error) { throw $reply.error.desc }
    }

    function Run-Framebuffer([string]$Name,[string]$Cpu,[string]$Virt,[string]$Dtb,[bool]$Draw,[string]$Expected) {
        $probe=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
        $probe.Start()
        try { $port=$probe.LocalEndpoint.Port } finally { $probe.Stop() }
        $si=New-Object Diagnostics.ProcessStartInfo
        $si.FileName=$QemuExe
        $si.Arguments="-machine virt-9.2,virtualization=$Virt,secure=off -cpu $Cpu -accel tcg -m 128 -smp 1 -display none -monitor none -net none -serial stdio -kernel `"$kernel`" -qmp tcp:127.0.0.1:${port},server=on,wait=off"
        if ($Dtb) { $si.Arguments+=" -dtb `"$Dtb`"" }
        $si.UseShellExecute=$false
        $si.CreateNoWindow=$true
        $si.RedirectStandardInput=$true
        $si.RedirectStandardOutput=$true
        $si.RedirectStandardError=$true
        $p=New-Object Diagnostics.Process
        $p.StartInfo=$si
        $started=$false
        $client=$null
        $reader=$null
        $writer=$null
        try {
            if (!$p.Start()) { throw 'QEMU did not start' }
            $started=$true
            $stdout=$p.StandardOutput.ReadToEndAsync()
            $stderr=$p.StandardError.ReadToEndAsync()
            Start-Sleep -Milliseconds 1000
            if ($p.HasExited) { throw $stderr.Result }
            $client=[Net.Sockets.TcpClient]::new('127.0.0.1',$port)
            $net=$client.GetStream()
            $net.ReadTimeout=4000
            $reader=[IO.StreamReader]::new($net)
            $writer=[IO.StreamWriter]::new($net,[Text.UTF8Encoding]::new($false))
            $writer.AutoFlush=$true
            $hello=$reader.ReadLine() | ConvertFrom-Json
            if (!$hello.QMP) { throw 'Unexpected QMP greeting' }
            Send-Qmp $reader $writer @{execute='qmp_capabilities'}
            $commands=@('fbinfo','fbtest','mmuinit','fbtest')
            if ($Draw) { $commands+=@('fbon','clear','echo HDMI mirror proof','fboff') }
            $commands+=@('echo UART still works','heaptest','halt')
            $shots=@{}
            $tests=0
            foreach ($command in $commands) {
                foreach ($c in ($command+"`r`n").ToCharArray()) {
                    $p.StandardInput.Write($c)
                    $p.StandardInput.Flush()
                    Start-Sleep -Milliseconds 5
                }
                Start-Sleep -Milliseconds 220
                if ($command -eq 'fbtest') { ++$tests }
                $shot=if ($Draw -and $command -eq 'fbtest' -and $tests -eq 2) { 'bars' }
                    elseif ($Draw -and $command -eq 'echo HDMI mirror proof') { 'text' }
                    elseif ($Draw -and $command -eq 'fboff') { 'off' }
                    elseif ($Draw -and $command -eq 'echo UART still works') { 'serial' }
                if ($shot) {
                    $file=Join-Path $out "$Name-$shot.bin"
                    Send-Qmp $reader $writer @{execute='pmemsave';arguments=@{
                        val=0x46000000-$guard;size=$stride*$height+2*$guard;filename=$file
                    }}
                    $shots[$shot]=[IO.File]::ReadAllBytes($file)
                }
            }
            if (!$p.HasExited) { $p.Kill() }
            $p.WaitForExit()
            $log=$stdout.Result
            $log | Set-Content -Encoding ASCII (Join-Path $out "$Name.log")
            $stderr.Result | Set-Content -Encoding ASCII (Join-Path $out "$Name.stderr.log")
            foreach ($fragment in @($Expected,'MMU enabled: kernel code RO executable','UART still works','PASS: heap alignment','CPU halted.')) {
                if (!$log.Contains($fragment)) { throw "$Name missing '$fragment' (see log)" }
            }
            if ($log.Contains('FATAL') -or $log.Contains('FAIL:') -or
                ($Draw -ne $log.Contains('Framebuffer test drawn.'))) { throw "$Name unexpected drawing or guest failure" }
            if ($Draw) {
                foreach ($fragment in @('Framebuffer requires the verified MMU mapping','OurOS HDMI console mirror on.','HDMI console mirror off.')) {
                    if (!$log.Contains($fragment)) { throw "$Name missing '$fragment'" }
                }
                $bars=$shots['bars']
                $colors=@(0xffff0000L,0xff00ff00L,0xff0000ffL,0xffffffffL)
                foreach ($y in @(0,($height-1))) {
                    for ($x=0;$x -lt $width;++$x) {
                        if ([BitConverter]::ToUInt32($bars,$guard+$y*$stride+$x*4) -ne $colors[[Math]::Floor($x/80)]) {
                            throw "$Name incorrect pixel at ($x,$y)"
                        }
                    }
                }
                foreach ($bytes in $shots.Values) {
                    for ($i=0;$i -lt $guard;++$i) {
                        if ($bytes[$i] -or $bytes[$bytes.Length-1-$i]) { throw "$Name changed a guard byte" }
                    }
                    for ($y=0;$y -lt $height;++$y) {
                        if ([BitConverter]::ToUInt64($bytes,$guard+$y*$stride+$width*4)) { throw "$Name changed row padding" }
                    }
                }
                $painted=$false
                for ($i=$guard;$i -lt $guard+$stride*20;$i+=4) {
                    if ([BitConverter]::ToUInt32($shots['text'],$i) -eq 0xffffffffL) { $painted=$true;break }
                }
                if (!$painted -or [Convert]::ToBase64String($shots['off']) -cne [Convert]::ToBase64String($shots['serial'])) {
                    throw "$Name text mirror or disable failed"
                }
            }
            Write-Output "PASS: $Name"
        } finally {
            if ($writer) { $writer.Dispose() }
            if ($reader) { $reader.Dispose() }
            if ($client) { $client.Dispose() }
            if ($started -and !$p.HasExited) { $p.Kill();$p.WaitForExit() }
            $p.Dispose()
        }
    }
    Run-Framebuffer 'framebuffer-a53' 'cortex-a53' 'off' $valid $true 'Framebuffer: firmware 320x192 x8r8g8b8 stride=1288'
    Run-Framebuffer 'framebuffer-a76' 'cortex-a76' 'on' $valid $true 'Framebuffer: firmware 320x192 x8r8g8b8 stride=1288'
    Run-Framebuffer 'framebuffer-bad-stride' 'cortex-a53' 'off' $bad $false 'Framebuffer: unsupported format or unsafe geometry.'
    Run-Framebuffer 'framebuffer-headless' 'cortex-a76' 'on' '' $false 'Framebuffer: no enabled firmware simple-framebuffer.'
} finally { Pop-Location }
