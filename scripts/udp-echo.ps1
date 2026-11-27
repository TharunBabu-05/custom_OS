# Echo from a specific Windows IPv4 address to the specified Pi.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ipaddress]$BindAddress,
    [Parameter(Mandatory)][ipaddress]$PeerAddress,
    [ValidateRange(1024,65535)][int]$Port=4040,
    [ValidateRange(1,300)][int]$TimeoutSeconds=120,
    [ValidateRange(1,4)][int]$Count=1
)
$ErrorActionPreference='Stop'
foreach ($address in @($BindAddress,$PeerAddress)) {
    $bytes=$address.GetAddressBytes()
    if ($bytes.Length -ne 4 -or !$bytes[0] -or $bytes[0] -ge 224) { throw 'Enter a specific unicast IPv4 address.' }
}
try { $socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new($BindAddress,$Port)) }
catch {
    $cause=$_.Exception.GetBaseException()
    if ($cause -is [Net.Sockets.SocketException] -and $cause.SocketErrorCode -eq [Net.Sockets.SocketError]::AddressNotAvailable) {
        throw "Windows cannot bind $BindAddress. Assign that IPv4 address to the adapter connected to the Pi and wait for AddressState Preferred."
    }
    throw
}
try {
    $socket.Client.ReceiveTimeout=250
    $remote=[Net.IPEndPoint]::new([Net.IPAddress]::Any,0)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $batch=$PSBoundParameters.ContainsKey('Count')
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $end=[double]$TimeoutSeconds
    Write-Output "Waiting for $PeerAddress on ${BindAddress}:$Port"
    while ($clock.Elapsed.TotalSeconds -lt $end) {
        try { $data=$socket.Receive([ref]$remote) }
        catch [Net.Sockets.SocketException] {
            if ($_.Exception.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { continue }
            throw
        }
        if (!$remote.Address.Equals($PeerAddress) -or $data.Length -gt 256) { continue }
        $key="$remote/$([Convert]::ToBase64String($data))"
        $known=$seen.Contains($key)
        if (!$known -and $seen.Count -eq $Count) { continue }
        if ($socket.Send($data,$data.Length,$remote) -ne $data.Length) { throw 'Incomplete UDP echo write.' }
        Write-Output "Echoed $($data.Length) bytes to $remote"
        if (!$batch) { return }
        if ($known) { continue }
        $null=$seen.Add($key)
        if ($seen.Count -eq $Count) { $end=[Math]::Min($end,$clock.Elapsed.TotalSeconds+4.5) }
    }
    if ($seen.Count -ne $Count) { throw "Echoed $($seen.Count) of $Count distinct datagrams from $PeerAddress before the timeout." }
    Write-Output "PASS: echoed $Count distinct UDP probes."
} finally { $socket.Dispose() }
