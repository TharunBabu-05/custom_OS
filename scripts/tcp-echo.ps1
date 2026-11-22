# Echo the 16 KiB test file once, from the specified Pi.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ipaddress]$BindAddress,
    [Parameter(Mandatory)][ipaddress]$PeerAddress,
    [ValidateRange(1024,65535)][int]$Port=4041,
    [ValidateRange(1,300)][int]$TimeoutSeconds=120
)
$ErrorActionPreference='Stop'
foreach ($address in @($BindAddress,$PeerAddress)) {
    $bytes=$address.GetAddressBytes()
    if ($bytes.Length -ne 4 -or !$bytes[0] -or $bytes[0] -ge 224) { throw 'Enter a specific unicast IPv4 address.' }
}
$listener=[Net.Sockets.TcpListener]::new($BindAddress,$Port)
$client=$null
try {
    $listener.Start(1)
    Write-Output "Waiting for $PeerAddress on ${BindAddress}:$Port"
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if (!$listener.Pending()) { Start-Sleep -Milliseconds 20;continue }
        $candidate=$listener.AcceptTcpClient()
        if ($candidate.Client.RemoteEndPoint.Address.Equals($PeerAddress)) { $client=$candidate;break }
        $candidate.Dispose()
    }
    if (!$client) { throw "No TCP connection from $PeerAddress before the timeout." }
    $client.NoDelay=$true
    $stream=$client.GetStream()
    $stream.ReadTimeout=10000
    $stream.WriteTimeout=10000
    $hash=[Security.Cryptography.SHA256]::Create()
    try {
        [byte[]]$buffer=New-Object byte[] 512
        for ($total=0;$total -lt 16384;) {
            if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { throw 'TCP echo timed out.' }
            $count=$stream.Read($buffer,0,[Math]::Min($buffer.Length,16384-$total))
            if (!$count) { throw "Peer closed after $total bytes." }
            $null=$hash.TransformBlock($buffer,0,$count,$buffer,0)
            $stream.Write($buffer,0,$count)
            $total+=$count
        }
        $null=$hash.TransformFinalBlock([byte[]]@(),0,0)
        $client.Client.Shutdown([Net.Sockets.SocketShutdown]::Send)
        if ($stream.Read($buffer,0,1)) { throw 'The Pi sent more than 16384 bytes.' }
        $hex=([BitConverter]::ToString($hash.Hash)).Replace('-','').ToLowerInvariant()
        Write-Output "Echoed 16384 bytes. SHA-256 $hex"
    } finally { $hash.Dispose() }
} finally {
    if ($client) { $client.Dispose() }
    $listener.Stop()
}
