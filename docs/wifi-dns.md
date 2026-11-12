# DNS and hostname ping

**Hardware verified:** the [new Pi capture](../proofs/pi5-dns-hostname-ping-clean-disconnect-2026-11-12.txt)
passes DNS and three replies each from `www.google.com` and `1.1.1.1`, followed
by explicit disconnect and acknowledged cleanup. The next OS phase and current
image are [kernel heap/RAMFS buffers](kernel-heap.md). The image measurements
below identify the earlier DNS image that produced this capture.

The [12 November Pi capture](../proofs/pi5-live-session-internet-ping-2026-11-12.txt)
confirms a live WPA3 session with an authorized peer, DHCP address
`10.21.217.100`, gateway/DNS `10.21.217.61`, ARP service and **three verified
replies from both the gateway and Internet address `1.1.1.1`**. The separately
reserved page bitmap at `0x40000000` and `pagetest` also pass on this Pi.
The transcript ends at `net>`; it does not establish long-duration stability
or record a disconnect/cleanup test for that particular run.

This revision adds `nslookup <hostname>` and `ping <hostname>` inside the live
console. DNS uses the first server from the DHCP ACK, then hostname ping uses
the existing IPv4/ICMP path with fresh ping randomness. **DNS and hostname ping
now pass on the Pi.** Package installation needs later storage, executable and TCP/HTTP
work; it is not provided by this revision.

## Image and fresh boot

Extract the full [disk ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the USB pendrive, or use the local
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). This is the complete
128 MiB disk, not the small kernel or a boot-files archive. Dated earlier
full images/ZIPs remain flat in `image_files`. The build also verifies the ZIP's
independently extracted image hash. No physical device is written by the scripts.

Use the existing CP2102 header UART at 115200 8N1, no flow control. Force PuTTY
Local echo and Local line editing off. Power-cycle, then run each command at
the normal `>` prompt, waiting for it to finish:

```text
meminfo
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

Enter the password only at the hidden prompt. No credentials are embedded or
saved. Skip the other join/SAE/association/DHCP/ping probes on this boot. Long
P256/H2E/SAE self-tests remain optional.

At **`net>`**, run these separately. Wait for each `DNS ...` or `Ping complete`
result before starting the next operation:

```text
netinfo
nslookup www.google.com
ping www.google.com
ping 1.1.1.1
netinfo
```

A successful lookup prints `DNS www.google.com: resolved -> <IPv4>` with the
answer TTL and number of CNAME hops. Hostname ping performs a fresh lookup,
prints the selected address and sends three ICMP requests. Addresses can change
between lookups. A reply mask of `7` means all three ping replies matched.
Ping timeout does not close Wi-Fi. A DNS error or timeout also leaves it open;
it does not start ICMP from an absent or unverified address.

Try `nslookup definitely-absent-ouros-test.invalid` to check the negative path.
An `NXDOMAIN` result is expected if the server reports the reserved invalid
name correctly; a timeout is also possible. Then try numeric ping again to
check that the connection is still usable. Leave the console idle for a minute
and repeat `netinfo`/ping to test continued service. End with:

```text
disconnect
```

Ctrl+C also requests disconnect between bounded I/O operations. At the normal
`>` prompt, run `netinfo`, `wifiinfo`, `storagetest`, `pagetest`, `uptime`.
Share the output from `wificonnect` through the network commands and final
cleanup/checks. Include the earlier bring-up output only if it fails or differs.
Do not include the password. Reboot before joining again.

## Resolver limits and checks

The original resolver implements one outstanding IPv4 A/IN query over UDP/53.
It normalizes ASCII hostnames, accepts an optional final root dot, checks the
63-byte label/253-character text limits and requests recursion from the DHCP
server. Wire encoding/compression follows [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035.html).
It accepts up to 512 DNS payload bytes, 32 total resource records and eight
CNAME links completed **within the same answer section**, including answers
listed out of chain order. It selects the first usable matching A address.
Authority/additional records never supply the selected address. CNAME-only
answers report no usable answer; follow-up queries for an incomplete alias
chain are a later extension.

Each query draws a new ID and source port from RNG200. Unbiased rejection
sampling uses the available port range 1024..65535, and repeated ID/port pairs
are refused. There is no timer/MAC fallback. Response matching checks the server
and local addresses, UDP endpoints, transaction ID, normalized question name,
type and class, following the query matching rules in
[RFC 5452](https://www.rfc-editor.org/rfc/rfc5452.html#section-9.1).
IPv4/header lengths, fragments, checksums and bounded backward name-compression
pointers are validated. Nonzero UDP checksums are checked; IPv4's optional zero
UDP checksum is accepted. Invalid traffic is ignored/counted without issuing
I/O from the receive callback. A response becomes a completed lookup only after
the query packet was successfully written.

There are at most three writes (normally at 0, 2 and 6 seconds), a 10-second
lookup deadline (also capped at 1001 service iterations if the clock stalls),
and the existing bounded transport/credit limits. Polling/I/O
can add visible wall-clock latency. The console continues servicing UART,
ARP, peer checks and supported M3/M4 retries during lookup. Cancellation never
starts a pending hostname ping. Lease expiry, conflicts or transport/link/RNG
failures retain the session teardown behavior.

The DNS server must be the gateway or an off-link unicast address reachable
through it. Other on-link neighbors need a future neighbor cache/ARP extension.
No EDNS, TCP fallback, IPv6/AAAA, DNSSEC validation, cache, search suffixes,
automatic CNAME follow-up or alternate resolver selection is implemented.
Truncated responses report that TCP is unavailable. DNS is ordinary plaintext
UDP, not an authenticated name service. No sockets, TCP/HTTP downloads, DHCP
renewal, group rekey or automatic reconnect are added. The foreground console
still ends on disconnect, lease expiry, its one-hour limit or a link/rekey error.

## Validation

- `scripts/test-dns.ps1`: 13 independent Node-generated exact DNS/Ethernet/
  IPv4/UDP fixtures, compressed/unordered CNAME chains and limits, additional
  isolation, negative replies, endpoint/transaction/question/checksum checks,
  truncations, 12000 guard-page mutations, loops/forward pointers, bounded
  retries, failed/early writes, lease expiry and port rejection sampling.
- `scripts/test-network-session.ps1`: lookup, hostname ping, fresh randomness,
  negative replies/timeouts keeping Wi-Fi open, cancellation and cleanup.
- Complete Wi-Fi model: DNS through authorized SAE/M4/key/port/DHCP setup,
  hostname and gateway pings past 35 seconds, over 256 data frames, DNS failures
  and disconnect, with deauthorization/DOWN/key deletion/private erasure.
- Typed SDPCM transport: maximum-length DNS query, exact owner validation,
  padding, credit-time cancellation and every write-stage failure.

`nettest` adds an offline DNS query/compressed-answer fixture on the kernel.
Native models and QEMU do not prove live DNS behavior on this hotspot; the
linked Pi capture supplies that hardware evidence for the DNS image.

## Packaged image

The full disk is **134217728 bytes** (128 MiB), ZIP **789835 bytes**, and
kernel `OUROS.IMG` **331196 bytes**. Full-disk SHA256:

```text
ef13adbb799a2b256b18d0a2cf32d586b82328a7af5a0dcc0c51ac367841cf96
```

Kernel/BSS/stack ends at `0x103000`, below the unchanged DTB at `0x200000`.
The separate bitmap reservation remains outside this span. Previous full
IMG/ZIP were preserved as `OurOS-Pi5-header-experimental-sd-20261112-193107-315.img`
and `OurOS-Pi5-header-experimental-sd-20261112-193112-791.zip` in `image_files`.

Native suite: `build/wifi-tests/phase-dns-suite.log`. The final association model
with the DNS service-iteration cap is in `build/wifi-tests/phase-dns-auth-final.log`;
the standalone DNS and console tests were rerun after that cap was added.
Memory tests also pass. Packaging verifies MBR/FAT/configuration and every
extracted file hash; the compression script verifies the decompressed disk hash.
`7z t` passes. Corruption/profile rejection checks pass in
`build/phase-dns-image-negative.log`; the original delivered image is unchanged.

Cortex-A53/A76 monitor, memory, filesystem/storage and exception tests pass in
`build/phase-dns-qemu.log`, including the new offline DNS fixture. The
EL2-to-EL1 USB2 BOT/FAT/firmware-file checks, 512-sector host-matched hash,
all DMA ring wraps and repeated mounts pass in `build/phase-dns-usb-qemu.log`.
Both test scripts confirm that the delivered disk hash is unchanged.
Known generated build outputs are cleaned after validation; current artifacts,
dated full-image/ZIP backups, ELF/maps and diagnostic logs are retained.
No physical device was written and no Git commit or push was performed.
