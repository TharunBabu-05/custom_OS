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
rules in the same PowerShell window.

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
```

### Run the UDP and TCP echo checks

Start the UDP echo server in Windows PowerShell.

```powershell
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
runio - - /boot/TCPCHK.ELF 192.0.2.1 4041
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

### Clean up Windows test settings

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
runio - - /boot/TCPCHK.ELF 192.0.2.1 4041
```

Expect both ends to report 16,384 bytes with the same SHA-256. Use the
existing TLS echo and package download procedures to verify an
authenticated exchange and a verified package install. [TLS echo](tls-live.md) · [Package download](package-download.md)

The Pi passed the Phase 52 wired TCP echo. Both endpoints reported
16,384 bytes and the same SHA-256. [Phase 52 wired TCP capture](../proofs/pi5-tcp-deadlines-2026-11-26.txt)

## Phase 53 TCP write half-close

`uos_tcp_shutdown_write` ends the send side of an established socket.
The stack acknowledges pending receive data and waits for queued sends
to be acknowledged before sending FIN. The handle continues to accept
reads and reports EOF after the peer closes its send side and the
receive buffer drains. [TCP half-close specification](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.6.1)

Repeated write shutdown succeeds. Further nonempty writes return
`UOS_EPIPE`. WRITE waiters receive `UOS_EVENT_WRITE_CLOSED`. Select
`UOS_EVENT_FIN_ACK` to wait for acknowledgment of the local FIN.
An app closes the handle after finishing reads.

The native TCP test covers partial acknowledgments, FIN retransmission,
post-FIN data and EOF. The QEMU TCP suite runs the EL0 half-close check
on Cortex-A53 and Cortex-A76. The Pi passed the client-first wired echo.
[Phase 53 Pi capture](../proofs/pi5-tcp-half-close-2026-11-26.txt)

### Pi 5 acceptance for Phase 53

Power off the Pi and connect its boot pendrive. In Windows PowerShell
from the project directory, preview and apply the update.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase53-tcp-half-close-boot-update.zip'
$volume = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volume.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volume[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject and boot. Follow the direct Ethernet setup through the
Windows firewall rules, then run the TCP listener below.
[Direct Ethernet setup](#set-up-the-direct-ethernet-link)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4041 -WaitForClientFin
```

Once Windows prints `Waiting`, run at `user:/$`.

```text
runio - - /boot/TCPCHK.ELF 192.0.2.1 4041 half
exit
```

Both sides should match the SHA-256 for 16,384 bytes. Windows should
report that it received the client FIN. The Pi should report that the
write half-close was acknowledged and the final reply arrived before EOF.
Run the cleanup checks at the monitor.

```text
ethdown
fileinfo
resinfo
storagetest
datacheck
```

[Clean up Windows test settings](#clean-up-windows-test-settings)

## Phase 54 network diagnostics

`netstat` shows the current TCP and UDP transport sessions. An active
session reports its IPv4 address, prefix, and slot usage. TCP splits
occupied slots into connecting, established, failed, closing, and
TIME-WAIT. A session can remain active while the Ethernet cable is unplugged.

TCP reports transmitted segments, retransmissions, acknowledged FINs,
resets and transport timeouts. TCP `bytes_tx` counts payload sent to
the driver, including retries. TCP `bytes_rx` counts bytes accepted into
an owned socket's receive buffer. UDP reports submitted and received
datagrams, payload bytes, receive queue drops and transport timeouts.
UDP `full_drops` counts valid peer datagrams rejected because the receive
slot already held a datagram. Both protocols report ARP requests and
resolution failures. These counters describe each transport session.
Counters remain visible after `ethdown` and reset when that transport
starts a new session with an incremented generation. Automatic driver
teardown and link shutdown clear the address and free all socket slots.

The sized `uos_net_stats` and `uos_socket_info` calls require network
permission. A socket snapshot requires ownership of the given TCP or UDP
handle. It reports readiness, peer IPv4 and ports, pending bytes, state
and any socket error. A UDP pending flag distinguishes an empty datagram
from an empty queue. TCP flags report peer FIN and write shutdown progress.
The queries copy metadata and leave the queues and pending errors intact.

Native TCP and UDP tests verify session resets, retained counters, slot
cleanup and ownership. The Cortex-A53 and Cortex-A76 QEMU runs also
passed the EL0 queries, network permission checks and redirection.
The Pi passed idle snapshots, checked output and permission tests.
The live traffic counter checks on the Pi are still pending.
[Phase 54 Pi netstat capture](../proofs/pi5-netstat-idle-2026-11-26.txt)

### Pi 5 acceptance for Phase 54

Power off the Pi and connect its boot pendrive. Run from the project
directory in Windows PowerShell.

```powershell
$pkg = 'build/rpi5-header/OurOS-Pi5-header-phase54-network-diagnostics-boot-update.zip'
$volume = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volume.Count -ne 1) { throw 'Connect exactly one OUROSBOOT pendrive.' }
$boot = $volume[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg
if ($LASTEXITCODE -eq 0) {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $pkg -Apply
}
```

Safely eject the drive and boot the Pi. In its shell, check the initial
session state and query permissions.

```text
netstat
netstat check
runlim 0 netstat check
```

Expect both sessions down, all slots free, and both checks to pass. Use
the direct Ethernet setup through `appshell` and the Windows firewall
commands. [Direct Ethernet setup](#set-up-the-direct-ethernet-link)

Start the UDP responder in Windows PowerShell. When it prints `Waiting`,
run the UDP command at the Pi `user:/$` prompt.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/udp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4040
```

```text
run /boot/UDPCHK.ELF 192.0.2.1 4040 wired-udp
```

Start the TCP responder in Windows PowerShell. When it prints `Waiting`,
run the TCP command followed by `netstat` at the Pi prompt.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tcp-echo.ps1 -BindAddress 192.0.2.1 -PeerAddress 192.0.2.2 -Port 4041 -WaitForClientFin
```

```text
runio - - /boot/TCPCHK.ELF 192.0.2.1 4041 half
netstat
exit
```

Expect both echo tools to pass their owned socket checks. `netstat`
should report both sessions up at `192.0.2.2/30`, one UDP send and
receive, one acknowledged TCP FIN, and the TCP payload byte counts.
TCP may retain a slot in TIME-WAIT after the app exits.

At the monitor `>` prompt, stop Ethernet and check the retained counters
and resource cleanup.

```text
ethdown
appterm 5 /boot/NETSTAT.ELF
fileinfo
resinfo
storagetest
datacheck
```

Expect both sessions down and zero occupied slots. The finished traffic
counters should still be readable.
[Clean up Windows test settings](#clean-up-windows-test-settings)

## Phase 55 Ethernet link recovery

`ethtcpup` and `ethudpup` start monitoring the onboard PHY along with
the socket worker. The worker checks the negotiated full-duplex speed
about every 200 ms and also samples after a DMA service error. On a
carrier loss or speed change it invalidates existing sockets, stops
GEM DMA, and waits for two matching usable speeds before reopening.
While DMA is active, the driver leaves the MAC configuration in place
and reads the PHY through the GEM management port.
[RP1 peripheral documentation](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)
The PHY hold and the claimed DMA page remain with the worker while it
waits. On recovery the worker configures the new speed and starts fresh
transport generations. TCP uses a fresh random secret. Apps must open
new sockets after the transition.

`ethstatus` shows the worker state, negotiated speed, interruptions,
successful recoveries, speed changes and last DMA/PHY results. `ethdown`
can stop an active or waiting worker. If GEM cannot verify that it stopped
using the DMA page, the driver retains that page and reports failure.

The host fixtures exercise active Clause 22 reads, carrier loss, GEM
restoration and a reopen at a different speed with invalid old handles.
On hardware, test a cable disconnect from an idle wired session and
reconnect it with the worker still running. During the break `ethstatus`
should report `waiting` and `netstat` should retain traffic counters with
zero occupied slots. After reconnect `ethstatus` should report `up` with
one recovery, and `netstat` should show a new generation with zero traffic
counters before the next echo.

The initial Pi run confirmed UDP traffic and clean shutdown. The status
samples showed generation 1 with zero interruptions and recoveries. The
TCP check reached 15,872 bytes before the shell stopped its child with
exit 124. [Initial Pi and Windows capture](../proofs/pi5-ethernet-recovery-initial-2026-11-26.txt)

Use `runio - - /boot/TCPCHK.ELF 192.0.2.1 4041 half` with the Phase 55
TCP retest package. Both streams still use the terminal. The `runio`
child gets a 30-second budget. The rebuilt check sends up to 512 bytes
per echo round trip and gives connection, transfer and close at most
24 seconds total. After disconnecting the cable, wait for `ethstatus`
to report `waiting` before reconnecting. The Windows adapter status
can confirm the physical link went down.

Both QEMU profiles passed a paced 16 KiB TCP half-close that took more
than ten seconds and finished under the `runio` budget. The Windows
responder also passed a paced loopback test with the same payload hash.

The Pi retest recorded one carrier loss, one recovery, and generation 2
for TCP and UDP. TCP passed the full 16 KiB half-close with matching
hashes. The two UDP probes in that capture had no reply.
[Pi recovery and TCP capture](../proofs/pi5-ethernet-recovery-2026-11-27.txt)

## Phase 56 UDP echo probes

`UDPCHK probe` runs one to four rounds on one socket. Each round sends
the process ID and round number in its payload. A retry uses the same
payload. The client only accepts the exact current payload as a match
and counts unexpected replies. Run it with `runio` to give the probe
its full 24-second app deadline within the 30-second child budget.

The UDP runtime waits for queue availability, confirms that the stack
handed the datagram to the driver, and waits for a reply under absolute
deadlines. A round retries a missed reply up to two times. Per-round
elapsed time includes retry delays. A send failure ends the probe with
its transport error. A sent datagram with no matching reply is reported
separately. Empty datagrams remain valid in the runtime and single echo.

The Windows listener's `-Count` parameter selects a bounded batch. A
repeated source endpoint and payload echoes again without advancing
the batch count. The completed batch accepts repeats for up to another
4.5 seconds. The default invocation still echoes once. A missing bind
address now reports which local IPv4 address Windows needs.

Both QEMU profiles passed the probe retry, mismatched reply, timeout
and cleanup checks. Windows loopback passed the single and batch echo
checks, including empty datagrams and duplicate requests. On the Pi,
all three probes matched on their first attempts. Windows logged twelve
echoes and three distinct requests, while the Pi counted two ignored
replies and five full-queue drops. [Phase 56 capture](../proofs/pi5-udp-probes-2026-11-27.txt)

## Phase 57 GEM transmit ring and counters

The wired DMA worker alternates between two transmit descriptors. It
publishes at most one frame at a time and leaves the next descriptor
host-owned, including across ring wrap. It checks
descriptor ownership, wrap placement and frame address before reusing
the transmit buffer and before releasing the DMA page.
[Cadence GEM descriptor organization](https://code.googlesource.com/linux/torvalds/linux/%2B/8abc1c8badc11c859bb4ba43211611347e59c55c/drivers/net/ethernet/cadence/macb_main.c)

`ethstats` shows submitted and completed descriptors, TX timeouts, RX
frames, discarded RX descriptors and RX-overrun observations. It also
samples and accumulates the GEM good-frame counters for TX and RX while
the worker owns the MAC. The counts cover all traffic handled by the
adapter. The worker retains statistics across cable recovery and after
`ethdown`. A new `ethudpup` or `ethtcpup` run starts a fresh report.
[GEM counter register definitions](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/cadence/macb.h)

The Windows batch listener prints distinct requests, repeats and total
echoes. Compare those counts with `netstat` and `ethstats` around an
otherwise idle three-probe run. The Phase 57 Pi retest matched all three
probes on their first attempts. Windows echoed three requests with zero
repeats. The Pi counted five submitted and completed frames, with matching
GEM TX and RX totals, zero queue drops, and zero RX overruns. The same run
passed the 16 KiB TCP half-close and released Ethernet resources.
[Phase 57 Pi and Windows capture](../proofs/pi5-gem-tx-ring-2026-11-27.txt)

The native RP1 model passed slot rotation, descriptor guards, MAC
counter sampling and retention across reopen. The Windows loopback
test counted two repeated arrivals in a three-request batch. Both QEMU
profiles passed platform command routing and TCP and UDP regressions.

## Phase 58 wired DHCP probe

`ethdhcp` uses the background network worker and an unconfigured GEM
session. Hardware entropy supplies a new transaction ID and randomized
delays. The shared DHCP client attempts up to three DISCOVERs and three
REQUESTs in a 45-second window. The GEM worker admits outbound DISCOVER
and initial REQUEST frames and sends incoming frames to the DHCP client.
The client confirms its transmit completion before accepting an ACK.
[DHCP client-server interaction](https://www.rfc-editor.org/rfc/rfc2131.html#section-3.1)

`ethlease` shows probe state, packet counts, and the latest offer or
acknowledgment. Metadata includes the address, server, lease duration, and
available mask, router, DNS, and renewal timer options. The probe requires
a negotiated full-duplex link and a directly reachable DHCP server. A PHY
link loss or speed change ends the probe. `ethdhcp cancel` stops it while
holding the PHY for another run, and `ethdown` also releases the PHY hold.
`ethstats` retains the probe's DMA and MAC counters after it finishes.

The plain probe records metadata. The host tests exercise a complete
exchange through the GEM model, retry exhaustion, cancellation, and DMA
restoration. The Pi verified cancellation and resource cleanup. The router
trial ended after one completed transmission, before an OFFER or ACK was
recorded. [Phase 58 Pi capture](../proofs/pi5-wired-dhcp-probe-2026-11-27.txt)

## Phase 59 wired DHCP LAN sessions

The `ethdhcp up` command obtains a fresh lease and checks the offered
address. It runs three ARP probes and two announcements through the same
GEM DMA session. During the check it listens for existing owners and
competing probes. If the check detects a conflict, it attempts a DHCPDECLINE
and enforces a ten-second retry delay for wired DHCP. The claim can finish
on a local network with no default router option.
[IPv4 address conflict detection](https://www.rfc-editor.org/rfc/rfc5227.html#section-2.1)
[DHCP client-server interaction](https://www.rfc-editor.org/rfc/rfc2131.html#section-3.1)

After the check, the DHCP address and mask provide local-subnet TCP and
UDP connectivity. `ethlease` reports `checking-address`, `bound`, and
terminal results, plus ARP counts and conflict details. `ethaddr` and
`netstat` show the active lease address. `ethdown`, lease expiration,
carrier interruption, or a later address conflict invalidate the sockets
and remove the lease from service. A later static session uses its
configured address. Wired renewal and routed traffic are future work.

The DHCP receiver counts non-DHCP traffic under `other`. `rx`, `ignored`,
and `malformed` cover DHCP candidates. The native GEM fixtures passed
acquisition, ARP announcement, duplicate-address refusal and DECLINE,
early expiry, conflict after activation, and DMA restoration. The Pi
sent three Discover frames and received no Ethernet frames in the second
hardware trial. TCP and UDP stayed down. The DHCP worker released its DMA
page during the EL0 shell, and the shell reported a resource mismatch.
[Phase 59 Pi and Windows capture](../proofs/pi5-wired-dhcp-timeout-2026-11-27.txt)

## Phase 60 DHCP wait and resource accounting

`ethdhcp wait` waits for acquisition and address checks to finish, then
prints the current lease result. Timer interrupts keep the network worker
and background services moving during the wait. Ctrl+C returns to the
monitor with the probe still running. A `bound` result identifies an
address ready for LAN traffic. A timeout with zero DHCP replies calls out
the need to check the DHCP server on the attached LAN.

`ethaddr` labels whether the displayed address is active. `ethaddr ip`
prints just the active IPv4 address for a Windows ping. The direct Windows
cable setup at `192.0.2.1/30` uses the static OurOS session at
`192.0.2.2/30`. The DHCP test requires a DHCP-enabled LAN.

The scheduler tracks the RP1 worker's DMA page separately from EL0
process pages. A page released by the worker changes the background
resource baseline, so a shell can finish after the worker times out.
`networktest cleanup` runs an EL0 sleeper across a background page
release and checks that the application cleanup still succeeds. The
Phase 60 Pi run passed that check and restored storage and resource
claims. It saw ordinary LAN traffic but received zero DHCP replies.
The Windows echo capture targeted the laptop's own address.
[Phase 60 Pi and Windows capture](../proofs/pi5-wired-cleanup-2026-11-27.txt)

## Phase 61 wired lease renewal

A bound `ethdhcp up` session renews by unicast with the leasing server at
T1. At T2 it broadcasts a rebinding request. `ethdhcp renew` queues an
earlier attempt. The GEM poll processes DHCP replies while dispatching
ordinary LAN traffic to the existing TCP and UDP sessions. A successful
ACK that preserves the current LAN configuration extends the deadline
without reopening sockets. An accepted NAK or lease expiry withdraws
the lease and closes the sockets. [DHCP renewal and expiry](https://www.rfc-editor.org/rfc/rfc2131.html#section-4.4.5)

`ethlease` shows renewal state, requests, rebroadcasts, completions, and
remaining lease seconds. `scripts/ping-rpi5-ethernet.ps1` rejects IP
addresses assigned to the Windows PC and counts successful replies from
the supplied Pi IPv4 address. The Pi booted Phase 61, negotiated at
100 Mbps, timed out after three Discover attempts, and refused the
renewal command with no lease. The same run released Ethernet claims
and passed the storage checks. [Phase 61 Pi and Windows capture](../proofs/pi5-wired-renewal-timeout-2026-11-27.txt)

## Phase 62 direct-cable DHCP acceptance

Run the fixture on an isolated cable between the laptop Ethernet adapter
and the Pi. Use the current Phase 61 image and a static `192.0.2.1/30`
on the chosen Windows adapter. The fixture offers `192.0.2.2/30` to the
OurOS Ethernet MAC `02:00:00:00:18:02`. It checks the ingress interface,
source, DHCP transaction and client identifier. Outbound Winsock packet
information pins the source address and interface. [Winsock packet information](https://learn.microsoft.com/en-us/windows/win32/api/ws2ipdef/ns-ws2ipdef-in_pktinfo)

The fixture advertises an 80-second lease with T1 at 25 seconds and T2 at
55 seconds. `Ack` accepts normal renewal, `Rebind` leaves unicast renewal
unanswered and accepts broadcast rebinding, `Nak` rejects renewal, and
`Silence` leaves subsequent requests unanswered so the lease expires.
Record the transaction lines on Windows and `ethlease` on the Pi.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rpi5-dhcp-fixture.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rpi5-dhcp-fixture.ps1 -InterfaceAlias 'Ethernet 3' -Mode Ack -TemporaryFirewallRule
```

Run the cable command in Administrator PowerShell when using the
temporary firewall rule. The fixture waits briefly for link negotiation
and for `192.0.2.1/30` to reach `Preferred`. It then opens UDP destination
port 67 with source port 68 on the selected adapter and removes its rule
on exit. The self-test covers packet handling and an interface-bound Winsock loopback
round trip. The OurOS C client accepted the fixture's OFFER and ACK and
decoded the address, mask and lease timers. The Windows fixture accepted
Discover and Request produced by that client. The direct-cable Pi run
then bound `192.0.2.2/30` with three ARP probes and two announcements.
Windows ACKed three distinct renewal requests 25 seconds apart. Capture
`ethlease` after T1 to record the Pi's completed renewal count. Physical
rebind, NAK, and expiry tests remain open. [Pi and Windows capture](../proofs/pi5-wired-dhcp-bound-2026-11-27.txt)
