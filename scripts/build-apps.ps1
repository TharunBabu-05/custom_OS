# Original freestanding sample executables. Outputs are regular project files.
[CmdletBinding()]
param([string]$OutputDirectory='build/apps')
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
try {
    $out=$OutputDirectory
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $flags=@('-std=c11','-Os','-Wall','-Wextra','-Werror','-Wstrict-prototypes',
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mgeneral-regs-only',
        '-mstrict-align','-mno-outline-atomics','-mabi=lp64','-mlittle-endian',
        '-fno-tree-loop-distribute-patterns','-ffunction-sections','-fdata-sections',
        '-mcpu=cortex-a53','-Iinclude','-Iuser/runtime')
    Invoke-Tool gcc @('-mcpu=cortex-a53','-c','user/runtime/start.S','-o',"$out/start.o")
    Invoke-Tool gcc ($flags+@('-c','user/runtime/memory.c','-o',"$out/memory.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/allocator.c','-o',"$out/allocator.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/allocator_svc.c','-o',"$out/allocator-svc.o"))
    Invoke-Tool gcc ($flags+@('-c','kernel/process/app_args.c','-o',"$out/app_args.o"))
    # Optimize the shell across its parser boundary.
    Invoke-Tool gcc ($flags+@('-fomit-frame-pointer','-flto','-c','kernel/process/app_args.c','-o',"$out/app_args-shell.o"))
    Invoke-Tool gcc ($flags+@('-c','lib/sha256.c','-o',"$out/sha256.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/tls_net.c','-o',"$out/tls_net.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/dir_walk.c','-o',"$out/dir_walk.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/wave.c','-o',"$out/wave.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/text_io.c','-o',"$out/text_io.o"))
    Invoke-Tool gcc ($flags+@('-c','user/runtime/ustar.c','-o',"$out/ustar.o"))
    Invoke-Tool gcc ($flags+@('-c','kernel/process/elf_app.c','-o',"$out/elf_app.o"))
    foreach ($source in @('crypto','p256','ecdsa','trust_time','pinned_cert','tls13_auth','tls13_keys','aes','aes_gcm','tls13_client','http_download')) {
        Invoke-Tool gcc ($flags+@('-c',"lib/$source.c",'-o',"$out/$source.o"))
    }
    foreach ($app in @('hello','counter','fault','badptr','files','leak','spin','busy','kern','text','xstack','echo','pwd','cat','ls','sleep','sleeper','dozer','toolchk','shell','ttychk','ttydeny','mutate','copychk','spawnchk','childchk','orphan','capchk','ttypar','iochk','ioraw','stream','hash','pipechk','datacap','datagen','mvchk','dirchk','replchk','exec','execchk','env','envchk','envrace','profchk','memchk','memdemo','heapchk','heapdemo','ps','v4chk','bigapp','df','udpchk','tcpchk','utccheck','certchk','tlschk','tlsecho','pkgget','grep','wc','head','find','du','cmp','hexdump','wavinfo','wavtone','wavedit','tail','cut','sort','uniq','tr','tar','fdchk','fddup','corechk','pkg32','treechk','schedchk','mapcat','vmchk','ports','ipcecho','chanchk','eventchk','limchk','sysstat')) {
        $name=$app.ToUpperInvariant()
        $appFlags=if ($app -eq 'shell') { $flags+@('-fomit-frame-pointer','-flto') } else { $flags }
        Invoke-Tool gcc ($appFlags+@('-c',"user/applications/$app.c",'-o',"$out/$app.o"))
        $extra=if ($app -eq 'shell') { @("$out/app_args-shell.o") } else { @() }
        if ($app -in @('hash','tcpchk')) { $extra+=@("$out/sha256.o") }
        if ($app -in @('certchk','tlschk','tlsecho','pkgget')) {
            $extra+=@('sha256','crypto','p256','ecdsa','trust_time','pinned_cert' | ForEach-Object { "$out/$_.o" })
        }
        if ($app -in @('tlschk','tlsecho','pkgget')) { $extra+=@("$out/tls13_auth.o","$out/tls13_keys.o") }
        if ($app -in @('tlsecho','pkgget')) { $extra+=@("$out/tls_net.o","$out/tls13_client.o","$out/aes.o","$out/aes_gcm.o") }
        if ($app -eq 'pkgget') { $extra+=@("$out/http_download.o","$out/elf_app.o") }
        if ($app -in @('wavinfo','wavtone','wavedit')) { $extra+=@("$out/wave.o") }
        if ($app -in @('tail','cut','sort','uniq','tr','sysstat')) { $extra+=@("$out/text_io.o") }
        if ($app -eq 'tar') { $extra+=@("$out/ustar.o","$out/text_io.o","$out/dir_walk.o") }
        if ($app -in @('find','du')) { $extra+=@("$out/dir_walk.o") }
        if ($app -in @('heapchk','heapdemo')) { $extra+=@("$out/allocator.o","$out/allocator-svc.o") }
        $limit=if ($app -in @('tlsecho','pkgget')) { 32768 } elseif ($app -in @('shell','heapchk','bigapp','certchk','tlschk')) { 16384 } elseif ($app -in @('envchk','memchk','heapdemo','ps','v4chk','tcpchk','tar','corechk','vmchk','chanchk','eventchk','limchk')) { 8192 } else { 4096 }
        if ($app -eq 'shell') {
            Invoke-Tool gcc (@('-flto','-Os','-fomit-frame-pointer','-nostdlib','-fno-pie','-Wl,--build-id=none','-Wl,--no-undefined','-Wl,--gc-sections',
                '-Wl,-z,max-page-size=4096',"-Wl,--defsym=__app_max_bytes=$limit",'-T','user/runtime/user.ld',
                "-Wl,-Map=$out/$name.map",'-o',"$out/$name.ELF","$out/start.o","$out/$app.o","$out/memory.o")+$extra)
        } else { Invoke-Tool ld (@('--build-id=none','--no-undefined','--gc-sections','-z','max-page-size=4096',
            "--defsym=__app_max_bytes=$limit",
            '-T','user/runtime/user.ld',"-Map=$out/$name.map",'-o',"$out/$name.ELF","$out/start.o","$out/$app.o","$out/memory.o")+$extra) }
        $undefined=Invoke-Tool nm @('-u',"$out/$name.ELF")
        # Preserve the actual executable only. Symbol/section metadata is not
        # needed by the bounded loader. Small programs fit /data; the expanded
        # shell uses the v4 persistent file bound.
        Invoke-Tool strip @('--strip-all',"$out/$name.ELF")
        if ($app -eq 'shell') {
            # The loader needs the ELF/program headers and sole LOAD segment.
            # Keep a disassembly before removing unused section-table metadata.
            Invoke-Tool objdump @('-d',"$out/$name.ELF") | Set-Content -Encoding ASCII "$out/$name.disassembly.txt"
            $minimal=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot "$out/$name.ELF"))
            $programAt=[int][BitConverter]::ToUInt64($minimal,32)
            if ($programAt -lt 64 -or $programAt+56 -gt $minimal.Length -or [BitConverter]::ToUInt16($minimal,56) -ne 1 -or
                [BitConverter]::ToUInt32($minimal,$programAt) -ne 1 -or [BitConverter]::ToUInt64($minimal,$programAt+8) -ne 0) { throw 'Unexpected shell LOAD layout' }
            $storedBytes=[int][BitConverter]::ToUInt64($minimal,$programAt+32)
            if ($storedBytes -lt $programAt+56 -or $storedBytes -gt $limit -or $storedBytes -gt $minimal.Length) { throw 'Invalid shell LOAD extent' }
            foreach ($index in (40..47)+(58..63)) { $minimal[$index]=0 }
            [byte[]]$compact=New-Object byte[] $storedBytes;[Array]::Copy($minimal,$compact,$storedBytes)
            [IO.File]::WriteAllBytes((Join-Path $ProjectRoot "$out/$name.ELF"),$compact)
        }
        $headers=Invoke-Tool readelf @('-h','-l','-d',"$out/$name.ELF")
        $headers | Set-Content -Encoding ASCII "$out/$name.readelf.txt"
        $text=$headers -join "`n"
        if ($text -notmatch 'Class:\s+ELF64' -or $text -notmatch 'Machine:\s+AArch64' -or
            $text -notmatch 'Type:\s+EXEC' -or $text -notmatch 'Data:.*little endian' -or
            $text -cmatch 'INTERP|NEEDED|DYNAMIC' -or $undefined) { throw "Invalid user ELF: $name" }
        $bytes=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot "$out/$name.ELF"))
        if (!$bytes.Length -or $bytes.Length -gt $limit) { throw "$name exceeds its configured executable bound" }
        $phoff=[BitConverter]::ToUInt64($bytes,32)
        $phnum=[BitConverter]::ToUInt16($bytes,56)
        if ($phnum -ne 1 -or [BitConverter]::ToUInt32($bytes,[int]$phoff) -ne 1 -or
            [BitConverter]::ToUInt32($bytes,[int]$phoff+4) -ne 5 -or
            [BitConverter]::ToUInt64($bytes,[int]$phoff+8) -ne 0 -or
            [BitConverter]::ToUInt64($bytes,[int]$phoff+16) -ne 0x2000000000L) { throw "$name unexpected LOAD geometry" }
        if ($app -ne 'shell') { Invoke-Tool objdump @('-d',"$out/$name.ELF") | Set-Content -Encoding ASCII "$out/$name.disassembly.txt" }
        Write-Output "PASS: $name.ELF AArch64 freestanding RX-only user executable, $($bytes.Length) bytes."
    }
} finally { Pop-Location }
