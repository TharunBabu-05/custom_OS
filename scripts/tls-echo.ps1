[CmdletBinding()]
param(
    [Parameter(Mandatory)][ipaddress]$BindAddress,
    [Parameter(Mandatory)][ipaddress]$PeerAddress,
    [ValidateRange(0,65535)][int]$Port=4042,
    [ValidateRange(1,600)][int]$TimeoutSeconds=300,
    [string]$OpenSsl='C:/msys64/usr/bin/openssl.exe',
    [string]$Package
)
$ErrorActionPreference='Stop'
foreach ($address in @($BindAddress,$PeerAddress)) {
    $bytes=$address.GetAddressBytes()
    if ($bytes.Length -ne 4 -or !$bytes[0] -or $bytes[0] -ge 224) { throw 'Enter a specific unicast IPv4 address.' }
}
$opensslPath=(Get-Command $OpenSsl -CommandType Application).Source
$node=(Get-Command node -CommandType Application).Source
$packageArgs=@()
if ($Package) {
    $file=Get-Item -LiteralPath $Package
    if ($file.PSIsContainer -or $file.Length -lt 64 -or $file.Length -gt 32768) { throw 'Package must be a file of 64 to 32768 bytes.' }
    $packageArgs+=@($file.FullName)
}
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('ouros-tls-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixture
Push-Location $fixture
try {
    @'
[ca]
default_ca=demo
[demo]
database=index.txt
new_certs_dir=.
serial=serial.txt
private_key=key.pem
default_md=sha256
policy=identity
x509_extensions=server
[identity]
commonName=supplied
[server]
subjectKeyIdentifier=none
authorityKeyIdentifier=none
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:demo.ouros.test
'@ | Set-Content -Encoding ASCII cert.cnf
    [IO.File]::WriteAllText((Join-Path $fixture 'index.txt'),'')
    Set-Content -Encoding ASCII serial.txt '01'
    & $opensslPath req -new -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -noenc -subj '/CN=demo.ouros.test' -keyout key.pem -out request.pem
    if ($LASTEXITCODE) { throw 'Could not generate a P-256 key' }
    $now=[DateTime]::UtcNow
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $start=$now.AddDays(-1).ToString("yyyyMMddHHmmss'Z'",$culture)
    $end=$now.AddDays(7).ToString("yyyyMMddHHmmss'Z'",$culture)
    & $opensslPath ca -batch -selfsign -config cert.cnf -in request.pem -startdate $start -enddate $end -notext -out cert.pem
    if ($LASTEXITCODE) { throw 'Could not generate the TLS certificate' }
    & $node "$PSScriptRoot/tls-echo.mjs" $BindAddress.IPAddressToString $PeerAddress.IPAddressToString $Port $TimeoutSeconds cert.pem key.pem @packageArgs
    if ($LASTEXITCODE) { throw 'TLS test server did not complete' }
} finally {
    Pop-Location
    Get-ChildItem -File -LiteralPath $fixture | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
    [IO.Directory]::Delete($fixture)
}
