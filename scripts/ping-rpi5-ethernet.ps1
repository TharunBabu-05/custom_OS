# Check reachability of the Pi from Windows using its active Ethernet IPv4 address.
[CmdletBinding()]
param([Parameter(Mandatory)][ipaddress]$PiAddress)
$ErrorActionPreference='Stop'
$bytes=$PiAddress.GetAddressBytes()
if ($bytes.Length -ne 4 -or !$bytes[0] -or $bytes[0] -eq 127 -or $bytes[0] -ge 224) {
    throw 'Enter the active unicast IPv4 address shown by ethaddr ip on the Pi.'
}
$target=$PiAddress.IPAddressToString
$local=@([Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() |
    ForEach-Object { $_.GetIPProperties().UnicastAddresses } |
    ForEach-Object { $_.Address.IPAddressToString })
if ($local -contains $target) { throw "$target belongs to this Windows PC. Enter the Pi address shown by ethaddr ip." }
$ping=[Net.NetworkInformation.Ping]::new()
$replies=0
try {
    foreach ($attempt in 1..3) {
        $reply=$ping.Send($PiAddress,1500)
        if ($reply.Status -eq [Net.NetworkInformation.IPStatus]::Success -and $reply.Address.Equals($PiAddress)) {
            ++$replies
            Write-Output "Reply from $target in $($reply.RoundtripTime) ms"
        } else {
            Write-Output "Probe $attempt status=$($reply.Status)"
        }
        if ($attempt -lt 3) { Start-Sleep -Milliseconds 250 }
    }
} finally { $ping.Dispose() }
if (!$replies) { throw "No echo reply from the Pi at $target." }
Write-Output "PASS: $replies/3 echo replies from $target."
