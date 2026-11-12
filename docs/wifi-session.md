# Foreground Wi-Fi session and numeric IPv4 ping

**This phase now has a [physical Pi capture](../proofs/pi5-live-session-internet-ping-2026-11-12.txt):
gateway and `1.1.1.1` each returned three replies at `net>`, and the external
bitmap/page test passed. The new image and next test are [DNS/hostname ping](wifi-dns.md).**
The measurements and prospective test instructions below describe the earlier
numeric-session revision, before that capture; they are retained as phase history.

The [new Pi transcript](../proofs/pi5-gateway-three-replies-2026-11-11.txt)
passes DHCP, address checks, announcements, gateway ARP and **all three gateway
echo replies**, with observed latencies of 26, 23 and 24 ms. The phone screenshot
shows the same OurOS MAC (`2c:cf:67:f4:99:75`) and leased IP (`10.21.217.100`).
The disconnect was deliberate: `wifiping` finishes its bounded test by removing
the address, deauthorizing the peer and bringing Wi-Fi DOWN. The log confirms
normal cleanup. It was not a failed gateway ping. The station's aggregate TX
failure counter is not a per-ping result; the verified reply mask is 7.

This revision adds **`wificonnect <SSID>`**. After the same verified
SAE/M3/M4/key/port/DHCP/address-check/gateway sequence, it opens a foreground
`net>` console and keeps servicing the connection. It stays open while waiting
for commands instead of disconnecting after the initial three pings. This new
session/routed-ping behavior needs a physical Pi test.

## Image

Complete disk: **134217728 bytes** (128 MiB); ZIP: **786110 bytes**;
kernel `OUROS.IMG`: **322660 bytes**. Full-disk SHA256:
`ff31a209bd017bf2a2e9114597aeef4c3f897c5e929a6e73cabae87eeb2f6dbb`.
Previous full images and ZIPs remain under dated names in `image_files`.
Kernel/BSS/stack now ends at `0x101000`, below the unchanged firmware DTB
region at `0x200000`. The separately reserved bitmap is outside that span.
The new arena placement and foreground session remain hardware untested.

## Run on the Pi

Extract the complete [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the USB pendrive, or flash the local
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). Power-cycle the Pi.
Use the existing CP2102/header UART, 115200 8N1, no flow control. Force PuTTY
Local echo and Local line editing off.

Run separately at the normal OurOS `>` prompt:

```text
meminfo
memmap
pagetest
nettest
keytest
storageinit
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
wificonnect Jack's
```

Wait for each prompt. Enter the password only at the hidden local prompt;
it is not saved or embedded. Skip `wifiping`, `wifidhcp`, `wifiassoc` and the
other join/SAE probes in this boot. Long P256/H2E/SAE self-tests are optional.

The intended new marker is:

```text
Wi-Fi session OPEN. Commands: ping [numeric IPv4], netinfo, help, disconnect.
net>
```

At **`net>`**, try:

```text
netinfo
ping
ping 1.1.1.1
netinfo
```

Wait for `Ping complete` before starting another ping. `ping` without an
argument targets the DHCP gateway. `ping 1.1.1.1` tests an off-link numeric
address through that gateway; no DNS query is involved. A matching public-IP
reply would provide evidence of external IPv4 reachability. The native model
cannot prove the phone/carrier permits those replies. **An unanswered ping
reports a timeout and leaves the Wi-Fi session open.** Other commands and
disconnect remain available while a ping is in progress.

Leave it idle for at least one minute, check that the Pi remains in the phone's
connected-device list, then try another ping and `netinfo`. This tests continued
service beyond the old 30-second probe window. To return to the OS monitor:

```text
disconnect
```

Ctrl+C also requests a disconnect, checked between bounded I/O operations.
Expect explicit closing/cleanup output and the normal `>` prompt. Then run:

```text
netinfo
wifiinfo
storagetest
pagetest
uptime
```

Share the output from `wificonnect`, the live commands/idle test, and the final
cleanup and storage/page checks. Do not include the password. Reboot before
joining again after disconnect or failure; the existing one-shot initialization
and key-installation journal remain in place.

## Session scope and termination

- The network console runs inside the verified Wi-Fi scope. While it waits
  for UART input it polls SDIO, services authenticated M3/M4 retries and queued
  ARP replies, and checks lease validity. It does not return an active private
  context to the general monitor. General OS commands are available after
  `disconnect`; there is no background scheduler or network worker yet.
- Every 15 seconds, and on `netinfo`, query the same firmware peer and check
  authenticated/associated/authorized flags, BSSID/SSID and SAE/AES/required-PMF
  policy. A stale/mismatched/disconnected peer stops the session. These are
  periodic checks, not an independent guarantee of uninterrupted RF delivery.
- Sessions stop on explicit disconnect, conservative lease expiry, the one-hour
  lifetime cap, the 360000-iteration cap, an address conflict, changed gateway
  MAC, unsupported rekey, lost authorization, RNG failure or transport error.
  **Lease renewal and automatic reconnect are not implemented.** The previously
  observed lease was about one hour; its current value is always taken from
  DHCP. Expiry is conservatively measured from acquisition start and checked
  again before credit-delayed packet writes. No expired address is kept in use.
  This follows the expiry requirement in
  [DHCP/RFC2131](https://www.rfc-editor.org/rfc/rfc2131.html).
- On exit, remove the temporary IP, attempt DHCPDECLINE for a detected conflict,
  explicitly deauthorize, DOWN, delete attempted keys, clear the RSN override,
  restore policy/events and erase private key state. Unconfirmed cleanup blocks
  further transport use. No key reinstallation is introduced by session pings.
  No DHCP RELEASE is sent yet; the server may retain its lease until expiration.
- The old lifetime cap of 256 received data frames remains during setup. In
  the open console, RX remains bounded per polling/transport operation with a
  rolling 64-frame allowance instead of a 256-frame lifetime cutoff. Integer
  overflow is refused. ARP has one pending reply slot and a 64-reply/minute
  limit. Existing M3/duplicate/M4/credit limits remain; this is not a complete
  persistent supplicant or group rekey implementation.

Termination reason numbers in `netinfo`: 1=user, 2=lease expiry, 3=link/peer
failure, 4=session/iteration limit, 5=I/O or RNG failure, 6=address conflict.
An initial setup failure can occur before any session opens. The overall
association result can also fail after a normal console exit if cleanup fails.

## Numeric ping boundaries

Off-link unicast IPv4 targets use the learned gateway MAC; the IP destination
remains the requested numeric address. The gateway's own IP is also supported.
Other on-link neighbors are refused until a broader neighbor cache is added.
Subnet/broadcast, multicast, loopback and invalid textual addresses are refused.
Each ping gets a fresh hardware-random identifier/token and matches reply
source/destination, payload, sequence and checksums, so an earlier ping's reply
cannot satisfy a later request. Three completed requests and at least one
verified reply produce success; reply mask 7 means all three replied. Observed
latencies include polling and SDIO/control processing. A ping has a ten-second
operation bound in addition to its usual request/reply waits.

Routing definitions: [IPv4 host requirements/RFC1122](https://www.rfc-editor.org/rfc/rfc1122.html).
This remains a narrow ARP/ICMP implementation: no DNS, TCP, TLS, sockets,
package installation, ICMP error decoding or inbound echo-request responses.
`ping www.google.com` is refused with a numeric-address explanation.

## Allocator bitmap relocation

The previous image's static 1 MiB physical-page bitmap left only one 4 KiB page
before the firmware DTB region. The bitmap now occupies an explicitly reserved,
page-aligned contiguous RAM arena selected from the normalized DT memory map.
Selection excludes kernel/stack, the DTB, boot guards and all firmware/dynamic
reservations, stays below the existing 32 GiB physical-address limit, and fails
without touching RAM if no safe arena exists. Initialization receives bounded
aligned storage. Every bitmap page is excluded from normal allocation/free.

`memmap` adds `reason=physical page bitmap`; `meminfo` prints its address/size.
The bitmap's 1 MiB cost now appears as a separate reservation, so managed RAM
can be 1 MiB lower than in older screenshots. The address limit and 4 KiB page
size are unchanged. Firmware entry, DTB placement and Image-size checks are
unchanged. This removes the large static object from the kernel image's runtime
span and makes room for networking without overwriting firmware data. This new
arena placement also needs verification in the next Pi memory/storage tests.

## Validation

Native memory tests cover bitmap selection, overlap/alignment/reservation/cap
failures, excluded bitmap pages, allocator uniqueness/free rejection, bounded
storage, DT parsing and guard-page mutations. Independent Node fixtures check
exact gateway and off-link IPv4/ICMP bytes. Session models cover responsive
input, ARP service, periodic peer checks, fresh-token replay rejection, manual
disconnect, unanswered pings remaining connected, lease/link/conflict/RNG/I/O
failure cleanup and long-line/edit handling. The complete Wi-Fi model performs
routed and gateway pings beyond 35 seconds, processes over 256 data frames,
and verifies deauthorization, key deletion and private erasure on exit.

These models do not prove the physical foreground session or external Internet
reachability. The next Pi transcript is required for those claims.

Completed checks for this image:

- Native memory/guard-page tests: `scripts/test-memory.ps1`.
- Full native Wi-Fi/SDIO/crypto suite: `build/wifi-tests/phase-session-suite.log`.
- Updated complete session/cleanup model, including over 256 received frames:
  `build/wifi-tests/phase-session-auth.log`. The standalone session test also
  covers long-line/edit behavior and continued service beyond 30 seconds.
- Cortex-A53/A76 monitor, external bitmap, memory/filesystem/storage and fault
  checks: `build/phase-session-qemu.log`; delivered disk hash unchanged.
- USB2 BOT/FAT/firmware assets, all ring wraps and EL2-to-EL1 handoff:
  `build/phase-session-usb-qemu.log`; delivered disk hash unchanged.
- MBR/FAT/kernel corruption and console-profile mismatch rejection:
  `build/phase-session-image-negative.log`.
- ZIP integrity and independently extracted full-disk SHA256 verification.

The previous full image/ZIP were preserved as
`OurOS-Pi5-header-experimental-sd-20261111-220338-392.img` and
`OurOS-Pi5-header-experimental-sd-20261111-220344-990.zip` in `image_files`.
Known generated build outputs are cleaned after validation. No physical device
was written and no Git commit or push was performed.
