# Phase 9 local TCP client sockets

An EL0 application can connect to a unicast IPv4 peer on the same subnet
as the active background Wi-Fi session. The worker resolves the peer
with ARP, performs the TCP handshake, and moves bytes between the socket
and the radio. Connections are outbound. The send queue holds up to
512 bytes, and the receive window holds up to 1024 bytes. A process can
own two connections. There are three slots total, including sockets whose
close is still in progress. [TCP specification](https://www.rfc-editor.org/rfc/rfc9293.html)

## Application API

The EL0 wrappers in `user/runtime/user.h` use syscalls 50–54. Pass the
peer's four IPv4 octets and a port in host byte order. Leave `reserved`
zero. The kernel chooses the source port. The worker seeds initial TCP
sequence numbers with the board RNG when a background session starts.

| Call | Result |
| --- | --- |
| `uos_tcp_open(&peer)` | Positive handle for an in-progress connection |
| `uos_tcp_status(handle)` | `UOS_EAGAIN` during connection setup or 0 when established |
| `uos_tcp_write(handle, data, bytes)` | Number of bytes queued, or `UOS_EAGAIN` while earlier bytes await acknowledgement |
| `uos_tcp_read(handle, data, capacity)` | Number of stream bytes copied, `UOS_EAGAIN` while empty, or 0 at EOF with a nonempty buffer |
| `uos_tcp_close(handle)` | Releases the handle and queues orderly shutdown of an established connection |

Reads and writes transfer at most 512 bytes per call. Applications can
sleep briefly before retrying `UOS_EAGAIN` and set their own deadlines.
Once connected, there is one data segment in flight. Acknowledged bytes
leave the send queue. Retransmission uses exponential backoff, and the
receiver acknowledges the first missing byte when later data arrives.
Closing connections and completed connections in TIME_WAIT retain their
slots for bounded intervals. An owner exit or successful `exec` releases
its handles. Link loss clears every connection.

This first increment is LAN-only and uses fixed bounds. It handles peer
FIN and RST, the advertised MSS and receive window, partial acknowledgements,
and sequence wraparound. Listening sockets, out-of-order buffering, RTT
estimation, TCP window scaling and Internet routing are later work.
[Retransmission timer specification](https://www.rfc-editor.org/rfc/rfc6298.html)

## Raspberry Pi and Windows test

In PowerShell in `C:\Users\tharu\custom_OS`, update the mounted `OUROSBOOT`
volume. Adjust `$boot` if Windows assigns another drive letter.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase9-tcp-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

The preview against Phase 8 selected `OUROS.IMG` and added `TCPCHK.ELF`.
Safely eject the pendrive and boot the Pi. Use the same access point used
for the UDP test. In PuTTY type `exit` from `user:/$`, then run the following
at `>`. Enter the hotspot password at the hidden prompt.

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

Check `TCP sockets=0 enabled=yes` in PuTTY. Verify `ACK address=` for the Pi
and run this in **Windows PowerShell** to check the laptop address.

```powershell
Get-NetIPAddress -InterfaceAlias Wi-Fi -AddressFamily IPv4
```

If your addresses are still the ones used for the successful UDP test,
start the listener in Windows PowerShell with this command.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 10.103.105.51 -PeerAddress 10.103.105.102 -Port 4041
```

Leave that window waiting. In **PuTTY** at `>`, run this command. Keep the
space between the laptop address and port `4041`.

```text
appterm 60 /boot/TCPCHK.ELF 10.103.105.51 4041
```

The app sends `/boot/LARGE.BIN`, checks the returned bytes, and reads EOF
before closing. PuTTY should show `PASS: TCP echoed 16384 bytes. SHA-256`
and `state=exited code=0`. Windows should print `Echoed 16384 bytes. SHA-256`
with the same digest. The standard boot fixture's digest is
`a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3`.
At the `>` monitor, run `netinfo` to confirm
`TCP sockets=0 enabled=yes`. Use `netdown` when you finish.

## Verification

Native tests exercise a lost SYN, lost data, partial acknowledgements,
reordered and duplicate data, overlap, bad checksums, sequence wraparound,
timeouts, reset, and link loss. On both QEMU profiles, the modeled peer
dropped an initial SYN and the EL0 app retried and verified the 16 KiB
file's SHA-256. The prior UDP socket tests passed on both profiles. The
Windows helper passed a 16 KiB loopback transfer and an orderly close.
The Pi-to-Windows TCP exchange remains open.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase9-tcp-boot-update.zip` | `3c9e054dd1c1b448c6f8af880b522b2cf14c38804da12381770ddcae36b4a61d` |
| `OUROS.IMG` | `bd5c501403178c22687e9c180c7c501794fb319b10ec35dac7b4497d8cc5f688` |
