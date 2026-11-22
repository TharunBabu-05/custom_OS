[CmdletBinding()]
param([string]$HostCompiler='C:/msys64/mingw64/bin/gcc.exe',[string]$Package)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/windows-tools.ps1"
Push-Location $ProjectRoot
$server=$null
try {
    $work=(New-Item -ItemType Directory -Path ('build/tls-live-test-'+[guid]::NewGuid().ToString('N'))).FullName
    $client=Join-Path $work 'client.exe'
    $stdout=Join-Path $work 'server.log'
    $stderr=Join-Path $work 'server.stderr.log'
    $packageArgs=@()
    $serverArgs='-BindAddress 127.0.0.1 -PeerAddress 127.0.0.1 -Port 0 -TimeoutSeconds 30'
    $confirmation='PASS: authenticated TLSv1.3 client Finished and echoed 9 encrypted bytes.'
    if ($Package) {
        $file=Get-Item -LiteralPath $Package
        $packageArgs+=@($file.FullName)
        $serverArgs+=" -Package `"$($file.FullName)`""
        $confirmation="PASS: authenticated TLSv1.3 client Finished and served $($file.Length) package bytes."
    }
    & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/tls13_client_host.c lib/tls13_client.c lib/tls13_auth.c lib/tls13_keys.c lib/pinned_cert.c lib/ecdsa.c lib/p256.c lib/trust_time.c lib/crypto.c lib/sha256.c lib/aes_gcm.c lib/aes.c lib/http_download.c kernel/process/elf_app.c -lws2_32 -lbcrypt -o $client
    if ($LASTEXITCODE) { throw 'TLS client test compilation failed' }
    if ($Package) {
        $parser=Join-Path $work 'http-download.exe'
        & $HostCompiler -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/http_download_host.c lib/http_download.c -o $parser
        if ($LASTEXITCODE) { throw 'HTTP test compilation failed' }
        & $parser
        if ($LASTEXITCODE) { throw 'HTTP download checks failed' }
    }
    $launcher=Join-Path $PSScriptRoot 'tls-echo.ps1'
    $server=Start-Process powershell -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$launcher`"",$serverArgs
    $null=$server.Handle
    $port=$pin=''
    for ($i=0;$i -lt 100 -and !$server.HasExited;++$i) {
        $log=Get-Content -Raw -LiteralPath $stdout
        if ($log) {
            $port=[regex]::Match($log,'on 127\.0\.0\.1:(\d+)').Groups[1].Value
            $pin=[regex]::Match($log,'trustadd ([0-9a-f]{64})').Groups[1].Value
        }
        if ($port -and $pin) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!$port -or !$pin) { throw "TLS echo server did not start. See $stderr" }
    & $client $port $pin @packageArgs
    if ($LASTEXITCODE) { throw "TLS client test failed. See $stdout and $stderr" }
    if (!$server.WaitForExit(5000)) { throw "TLS server did not exit after the exchange. See $stdout and $stderr" }
    if ($server.ExitCode -ne 0) { throw "TLS server exited with code $($server.ExitCode). See $stderr" }
    $log=Get-Content -Raw -LiteralPath $stdout
    if (!$log.Contains($confirmation)) { throw "No OpenSSL confirmation. See $stdout" }
    $log
} finally {
    if ($server) { $server.Dispose() }
    Pop-Location
}
