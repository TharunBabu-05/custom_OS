[CmdletBinding()]
param([string]$OpenSsl='C:/msys64/usr/bin/openssl.exe')
. "$PSScriptRoot/windows-tools.ps1"

$app=Join-Path $ProjectRoot 'build/apps/PKG32.ELF'
if (!(Test-Path -LiteralPath $app -PathType Leaf)) { & "$PSScriptRoot/build-apps.ps1" }
$out=Join-Path $ProjectRoot ('build/repository-demo-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $out
$key=Join-Path $out 'demo-signing.pem'
& $OpenSsl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:prime256v1 -out $key
if ($LASTEXITCODE) { throw 'Demo signing key generation failed' }

function Member([string]$name,[string]$text) {
    $file=Join-Path $out $name
    [IO.File]::WriteAllText($file,$text,[Text.Encoding]::ASCII)
    return $file
}
function Identity([string]$name,[string]$version) { @{ name=$name; version=$version } }
$base1=Member 'base1.txt' 'base revision one'
$base2=Member 'base2.txt' 'base revision two'
$message1=Member 'demo1.txt' 'demo revision one'
$message2=Member 'demo2.txt' 'demo revision two'
$manifest=@{packages=@(
    @{name='pkgdemo';version='1';files=@{'main.elf'=$app;'message.txt'=$message1};requires=@((Identity 'pkgbase' '1'))},
    @{name='pkgdemo';version='2';files=@{'main.elf'=$app;'message.txt'=$message2};requires=@((Identity 'pkgbase' '2'))},
    @{name='pkgbase';version='1';files=@{'version.txt'=$base1}},
    @{name='pkgbase';version='2';files=@{'version.txt'=$base2}},
    @{name='pkgkeep';version='1';files=@{'message.txt'=$message1};requires=@((Identity 'pkgbase' '1'))},
    @{name='pkgloop';version='1';files=@{'message.txt'=$message1};requires=@((Identity 'pkgnext' '1'))},
    @{name='pkgnext';version='1';files=@{'message.txt'=$message1};requires=@((Identity 'pkgloop' '1'))},
    @{name='pkgmiss';version='1';files=@{'message.txt'=$message1};requires=@((Identity 'absent' '1'))}
)}
$json=Join-Path $out 'manifest.json'
[IO.File]::WriteAllText($json,($manifest | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$repo=Join-Path $out 'REPO.OPR'
$messages=@(& python "$PSScriptRoot/build-repository.py" --manifest $json --key $key --output $repo --openssl $OpenSsl)
if ($LASTEXITCODE) { throw 'Demo repository build failed' }
$pin=@($messages | Where-Object { $_ -match '^pkgtrust [0-9a-f]{64}$' })
if ($pin.Count -ne 1) { throw 'Missing demo public-key pin' }
[IO.File]::WriteAllLines((Join-Path $out 'PIN.TXT'),$pin,[Text.Encoding]::ASCII)
$messages
Write-Output "REPOSITORY: $repo"
