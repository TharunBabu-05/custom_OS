# Phase 51 socket readiness

`UOS_EVENT_UDP` and `UOS_EVENT_TCP` let apps wait for socket readiness
in the same event set as timers, IPC, signals, and child exits. Reading
event flags leaves socket data and pending transport errors available
for the regular socket calls.

| Source | READ | WRITE |
| --- | --- | --- |
| UDP | A datagram is queued, including an empty one | The send queue can accept a datagram |
| TCP | Data is buffered or the peer has finished sending | The connection is open and can accept another write |

`UOS_EVENT_ERROR` wakes either interest when a transport error is present.
Use the regular socket call to get the error code. `UOS_EVENT_HANGUP`
reports link loss or a socket handle unavailable to the caller. An
app needs the network permission to put socket entries in a wait set.
The runtime's `uos_socket_wait` is a convenience for one socket.

TCP EOF stays readable after the buffered data is drained, and
`uos_tcp_read` returns zero at EOF. A TCP peer that finishes sending
can still receive data. `TCPCHK.ELF` uses waits during connection
setup and stream I/O. `UDPCHK.ELF` uses a wait before receiving its reply.

The native tests cover pending transmit buffers, buffered data, errors,
EOF, link loss, and ownership. The QEMU UDP model exercises a timer and
socket in one wait set, with each source taking a turn waking the app.

On the Pi, `UDPCHK.ELF` matched the reply twice over the direct cable.
Windows confirmed the nine-byte reply and the Pi exited with code zero.
[Phase 51 wired UDP capture](../proofs/pi5-socket-readiness-udp-2026-11-26.txt)

## Pi 5 acceptance

Power off the Pi and connect its boot pendrive. Run from the project
directory in Windows PowerShell.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase51-socket-readiness-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

### Set up the direct Ethernet link

Safely eject and boot the pendrive with the direct Ethernet cable
connected. At `user:/$`, type `exit` and run these at `>`.

```text
ethaddr 192.0.2.2/30
ethup
ethprobe
```

Once `ethprobe` shows `link=up autoneg=complete`, run:

```text
ethtcpup
appshell
```

In administrator Windows PowerShell, go to the project directory.
Set `$nic` to the Windows adapter directly connected to the Pi.
The example uses `Ethernet 3`. Run this block to add the temporary
address if needed and confirm that Windows can use it.

```powershell
$nic = 'Ethernet 3'
$adapter = Get-NetAdapter -Name $nic -ErrorAction Stop
if ($adapter.Status -ne 'Up') { throw "Connect the Pi cable to $nic first." }
$address = @(Get-NetIPAddress -IPAddress 192.0.2.1 -AddressFamily IPv4 -ErrorAction SilentlyContinue)
if ($address | Where-Object { $_.InterfaceIndex -ne $adapter.ifIndex -or $_.PrefixLength -ne 30 }) {
    throw '192.0.2.1 is already assigned with a different adapter or prefix.'
}
$addedTestAddress = !$address
if ($addedTestAddress) {
    $priorDhcp = (Get-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4).Dhcp
    New-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 -PrefixLength 30 -PolicyStore ActiveStore -ErrorAction Stop
}
Get-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 |
    Select-Object IPAddress,PrefixLength,AddressState
```

Wait for `AddressState` to show `Preferred`. If it shows `Tentative`, run
the last query again after a moment. Check the direct link before starting
the echo tests.

```powershell
ping.exe -n 1 -w 1000 -S 192.0.2.1 192.0.2.2
```

After a reply from `192.0.2.2`, create or repair the two scoped firewall
rules and start the UDP server in the same PowerShell window.

```powershell
foreach ($test in @(@('UDP', 4040), @('TCP', 4041))) {
    $rule = @{
        Name = 'OurOS-Wait-' + $test[0]
        Direction = 'Inbound'
        Action = 'Allow'
        Enabled = 'True'
        Protocol = $test[0]
        LocalPort = $test[1]
        InterfaceAlias = $nic
        LocalAddress = '192.0.2.1'
        RemoteAddress = '192.0.2.2'
    }
    $label = 'OurOS direct Ethernet ' + $test[0] + ' echo'
    if (Get-NetFirewallRule -Name $rule.Name -ErrorAction SilentlyContinue) {
        Set-NetFirewallRule @rule -NewDisplayName $label -ErrorAction Stop
    } else {
        New-NetFirewallRule @rule -DisplayName $label -ErrorAction Stop
    }
}
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/udp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4040
```

When Windows prints `Waiting for 192.0.2.2 on 192.0.2.1:4040`,
run at `user:/$`.

```text
run /boot/UDPCHK.ELF 192.0.2.1 4040 wired-udp
```

Expect `PASS: UDP echo matched the sent datagram.` After Windows reports
one reply, start the TCP echo server in the same PowerShell window.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4041
```

With the TCP server waiting, run at `user:/$`:

```text
run /boot/TCPCHK.ELF 192.0.2.1 4041
exit
```

Expect 16,384 echoed bytes and matching SHA-256 values on both ends.
Run these at the monitor to verify cleanup.

```text
ethdown
timerobjects
fileinfo
resinfo
storagetest
datacheck
```

Remove the temporary rules in administrator PowerShell. If you added the
test address in this window, remove it and restore its prior DHCP setting.

```powershell
Remove-NetFirewallRule -Name 'OurOS-Wait-UDP','OurOS-Wait-TCP'
if ($addedTestAddress) {
    Remove-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1 -PolicyStore ActiveStore -Confirm:$false
    if ($priorDhcp -eq 'Enabled') {
        Set-NetIPInterface -InterfaceAlias $nic -AddressFamily IPv4 -Dhcp Enabled -PolicyStore ActiveStore
    }
}
```

## Phase 52 deadline-aware TCP runtime

`tcp_io.h` gives apps a shared wait for connection setup, timed reads
and writes, and exact reads and complete writes across multiple chunks.
Callers pass an absolute deadline from the `uos_time_ms` clock. The
runtime splits long waits into the event ABI's maximum wait intervals
without extending the original deadline. It returns `UOS_ETIMEDOUT`
when the deadline expires, `UOS_EINTR` on interruption, and `UOS_EOF`
if an exact read ends early.

`TCPCHK.ELF`, `TLSECHO.ELF`, and `PKGGET.ELF` use this runtime. TLS
connection failures close the socket before returning. The TLS exchange
and package download use the same deadline for all transport callbacks.

### Pi 5 acceptance for Phase 52

Power off the Pi and connect its boot pendrive. Run in PowerShell from
the project directory.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase52-tcp-deadlines-boot-update.zip'
$volume = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volume.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volume[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject and boot. Follow the direct Ethernet setup through the
Windows firewall rules. Then start the TCP listener on Windows and
run the TCP check on the Pi. [Direct Ethernet setup](#set-up-the-direct-ethernet-link)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4041
```

```text
run /boot/TCPCHK.ELF 192.0.2.1 4041
```

Expect both ends to report 16,384 bytes with the same SHA-256. Use the
existing TLS echo and package download procedures to verify an
authenticated exchange and a verified package install. [TLS echo](tls-live.md) · [Package download](package-download.md)
