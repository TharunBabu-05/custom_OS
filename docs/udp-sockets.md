# Phase 8 local UDP sockets

An EL0 application can open a UDP socket for a single unicast IPv4 peer on
the same subnet as an active `netup` session. The Wi-Fi worker resolves the
peer with ARP and transmits queued datagrams while EL0 runs. A socket owns
one send slot and one receive slot of up to 256 bytes each. There are four
sockets total and two per process. A full receive slot retains the first
datagram. [UDP](https://www.rfc-editor.org/info/rfc768/) · [ARP](https://www.rfc-editor.org/info/rfc826/)

## Application API

The wrappers in `user/runtime/user.h` use syscall numbers 46–49. Port
numbers use host byte order. `local_port=0` selects a port from 49152–65535.
A specified source port must be at least 1024 and unused. Incoming packets
must match the socket's peer address, peer port, and local port.

| Call | Result |
| --- | --- |
| `uos_udp_open(&peer)` | Positive process-owned handle |
| `uos_udp_send(handle, data, bytes)` | Number of bytes queued, or `UOS_EAGAIN` while another datagram is queued |
| `uos_udp_recv(handle, data, capacity, wait_ms)` | Size of the next datagram, including zero for an empty datagram |
| `uos_udp_close(handle)` | Zero on success |

Use `wait_ms=0` to poll. An empty socket returns `UOS_EAGAIN` immediately.
The kernel can also wait up to 10,000 ms and returns `UOS_ETIMEDOUT` at
the receive deadline. A buffer that is too small returns `UOS_EMSGSIZE`
and leaves the datagram queued. ARP resolution times out after three
seconds and reports `UOS_EHOSTUNREACH` to the next send or receive. A
successful send means the worker accepted the datagram for transmission.

Sockets close on process exit, cancellation, fault, timeout, and successful
`exec`. Children open their own sockets. Ending the Wi-Fi session clears
every socket and wakes blocked receives with `UOS_ENETDOWN`. After
reconnect, an application must open a new socket.

## Pi and Windows echo test

Use a 2.4 GHz WPA3 network with DHCP that allows the Pi and Windows PC to
communicate on the same subnet. Run this from `C:\Users\tharu\custom_OS`
once Windows mounts `OUROSBOOT`. Set `$boot` to the assigned drive letter.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase8-udp-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Safely eject the drive and boot the Pi. Type `exit` at `user:/$`. Run the
following at the `>` monitor, waiting for each command to finish. This
example uses `Jack's` with country code `IN`.

```text
wififiles
wifiinit
wificores
wifiload
wifistart
wificontrol
wificaps
wifiprepare IN
wifiscan
wifitarget Jack's
netup -R Jack's
netinfo
```

Enter the password at the hidden prompt after `netup`. Find the Pi's
`ACK address=` in `netinfo` and the PC's Wi-Fi `IPv4Address` in PowerShell.

```powershell
Get-NetIPConfiguration
```

Set `$pcIp` and `$piIp` to those two addresses. These are examples from a
single subnet. Keep this PowerShell window open while running the Pi check.

```powershell
$pcIp = '10.103.105.101'
$piIp = '10.103.105.102'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/udp-echo.ps1 -BindAddress $pcIp -PeerAddress $piIp -Port 4040
```

At the Pi monitor, type `appshell`, then run the following at `user:/$`.
Use the same PC address entered as `$pcIp` on Windows.

```text
run /boot/UDPCHK.ELF 10.103.105.101 4040 our-os-udp
```

The Pi should print `PASS: UDP echo matched the sent datagram.` and
`child exit=0`. PowerShell should report `Echoed 10 bytes` and return to its
prompt. Run the same Pi command while the echo helper is closed to check
`UDP echo timed out.`. Restart the helper with the same PowerShell command
and repeat the Pi command to verify recovery.

When finished, use the following to leave EL0, confirm
`UDP open sockets=0`, and end the Wi-Fi session.

```text
exit
netinfo
netdown
netinfo
```

## Local verification

The native Wi-Fi suite exercises exact packet bytes, checksum handling,
bound peer and port checks, queue limits, a failed ARP lookup, and retry
after the peer returns. Both QEMU profiles passed the real EL0
syscalls against a modeled peer, including cross-process handle rejection,
invalid user buffers, empty datagrams, timeouts, and link-loss wakeup.
The Windows echo helper passed a binary loopback test. The boot preview
against the Phase 7 group-key update selected `OUROS.IMG`, `PS.ELF`, and
`UDPCHK.ELF`. The Pi then passed the physical Pi-to-Windows echo test.
Windows echoed the 10-byte `our-os-udp` message, and `UDPCHK.ELF` matched
the returned bytes and exited with code 0. A separate physical test of
peer timeout and recovery remains open.
[Pi and Windows capture](../proofs/pi5-udp-echo-2026-11-22.txt)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-wifi.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
$build = powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
$image = ($build | Where-Object { $_ -like 'IMAGE: *' }) -replace '^IMAGE: ', ''
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -UdpSockets -Image $image
```

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase8-udp-boot-update.zip` | `bdfc333e0c5289b6e3230d20a3fb47a0a83f27d32415ddb3ecd1645ea1f64ec0` |
| `OUROS.IMG` | `6347506d5f9cb7471971b150c6373e8cd83f71a412ad4ada1b44ebcdda946dd0` |
