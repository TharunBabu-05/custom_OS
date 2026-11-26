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

Use the Windows direct-cable adapter with preferred address
`192.0.2.1/30`. The examples use the adapter named `Ethernet 3`.
The existing Ethernet guide has the Windows address setup.
[Direct Ethernet setup](platform-driver-plan.md#phase-18-ethernet-arp-over-the-cable)

In administrator Windows PowerShell, from the project directory, run:

```powershell
$nic = 'Ethernet 3'
Get-NetIPAddress -InterfaceAlias $nic -IPAddress 192.0.2.1
New-NetFirewallRule -Name 'OurOS-Wait-UDP' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 4040 -InterfaceAlias $nic -LocalAddress 192.0.2.1 -RemoteAddress 192.0.2.2
New-NetFirewallRule -Name 'OurOS-Wait-TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4041 -InterfaceAlias $nic -LocalAddress 192.0.2.1 -RemoteAddress 192.0.2.2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/udp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4040
```

With the UDP server waiting, run at `user:/$`:

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

Remove the temporary rules in the administrator PowerShell.

```powershell
Remove-NetFirewallRule -Name 'OurOS-Wait-UDP','OurOS-Wait-TCP'
```
