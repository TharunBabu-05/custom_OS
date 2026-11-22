# One reply to the specified Pi, on a specific Windows IPv4 address.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ipaddress]$BindAddress,
    [Parameter(Mandatory)][ipaddress]$PeerAddress,
    [ValidateRange(1024,65535)][int]$Port=4040,
    [ValidateRange(1,300)][int]$TimeoutSeconds=120
)
$ErrorActionPreference='Stop'
foreach ($address in @($BindAddress,$PeerAddress)) {
    $bytes=$address.GetAddressBytes()
    if ($bytes.Length -ne 4 -or !$bytes[0] -or $bytes[0] -ge 224) { throw 'Enter a specific unicast IPv4 address.' }
}
$socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new($BindAddress,$Port))
try {
    $socket.Client.ReceiveTimeout=250
    $remote=[Net.IPEndPoint]::new([Net.IPAddress]::Any,0)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    Write-Output "Waiting for $PeerAddress on ${BindAddress}:$Port"
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        try { $data=$socket.Receive([ref]$remote) }
        catch [Net.Sockets.SocketException] {
            if ($_.Exception.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { continue }
            throw
        }
        if (!$remote.Address.Equals($PeerAddress) -or $data.Length -gt 256) { continue }
        if ($socket.Send($data,$data.Length,$remote) -ne $data.Length) { throw 'Incomplete UDP echo write.' }
        Write-Output "Echoed $($data.Length) bytes to $remote"
        return
    }
    throw "No datagram received from $PeerAddress before the timeout."
} finally { $socket.Dispose() }
