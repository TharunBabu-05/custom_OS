# Original freestanding sample executables. Outputs are regular project files.
[CmdletBinding()]
param()
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out='build/apps'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $flags=@('-std=c11','-Os','-Wall','-Wextra','-Werror','-Wstrict-prototypes',
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mgeneral-regs-only',
        '-mstrict-align','-mno-outline-atomics','-mabi=lp64','-mlittle-endian',
        '-fno-tree-loop-distribute-patterns','-ffunction-sections','-fdata-sections',
        '-mcpu=cortex-a53','-Iinclude','-Iuser/runtime')
    Invoke-Tool gcc @('-mcpu=cortex-a53','-c','user/runtime/start.S','-o',"$out/start.o")
    Invoke-Tool gcc ($flags+@('-c','user/runtime/memory.c','-o',"$out/memory.o"))
    Invoke-Tool gcc ($flags+@('-c','kernel/process/app_args.c','-o',"$out/app_args.o"))
    foreach ($app in @('hello','counter','fault','badptr','files','leak','spin','busy','kern','text','xstack','echo','pwd','cat','ls','sleep','sleeper','dozer','toolchk','shell','ttychk','ttydeny','mutate','copychk','spawnchk','childchk','orphan','capchk','ttypar','iochk','ioraw')) {
        $name=$app.ToUpperInvariant()
        Invoke-Tool gcc ($flags+@('-c',"user/applications/$app.c",'-o',"$out/$app.o"))
        $extra=if ($app -eq 'shell') { @("$out/app_args.o") } else { @() }
        Invoke-Tool ld (@('--build-id=none','--no-undefined','--gc-sections','-z','max-page-size=4096',
            '-T','user/runtime/user.ld',"-Map=$out/$name.map",'-o',"$out/$name.ELF","$out/start.o","$out/$app.o","$out/memory.o")+$extra)
        $undefined=Invoke-Tool nm @('-u',"$out/$name.ELF")
        # Preserve the actual executable only. Symbol/section metadata is not
        # needed by the bounded kernel loader, and the disk files fit /data.
        Invoke-Tool strip @('--strip-all',"$out/$name.ELF")
        $headers=Invoke-Tool readelf @('-h','-l','-d',"$out/$name.ELF")
        $headers | Set-Content -Encoding ASCII "$out/$name.readelf.txt"
        $text=$headers -join "`n"
        if ($text -notmatch 'Class:\s+ELF64' -or $text -notmatch 'Machine:\s+AArch64' -or
            $text -notmatch 'Type:\s+EXEC' -or $text -notmatch 'Data:.*little endian' -or
            $text -cmatch 'INTERP|NEEDED|DYNAMIC' -or $undefined) { throw "Invalid user ELF: $name" }
        $bytes=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot "$out/$name.ELF"))
        if (!$bytes.Length -or $bytes.Length -gt 4096) { throw "$name exceeds the persistent-file bound" }
        $phoff=[BitConverter]::ToUInt64($bytes,32)
        $phnum=[BitConverter]::ToUInt16($bytes,56)
        if ($phnum -ne 1 -or [BitConverter]::ToUInt32($bytes,[int]$phoff) -ne 1 -or
            [BitConverter]::ToUInt32($bytes,[int]$phoff+4) -ne 5 -or
            [BitConverter]::ToUInt64($bytes,[int]$phoff+8) -ne 0 -or
            [BitConverter]::ToUInt64($bytes,[int]$phoff+16) -ne 0x2000000000L) { throw "$name unexpected LOAD geometry" }
        Invoke-Tool objdump @('-d',"$out/$name.ELF") | Set-Content -Encoding ASCII "$out/$name.disassembly.txt"
        Write-Output "PASS: $name.ELF AArch64 freestanding RX-only user executable, $($bytes.Length) bytes."
    }
} finally { Pop-Location }
