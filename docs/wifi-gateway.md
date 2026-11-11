# Address checks, ARP and gateway ping

The [latest physical Pi capture](../proofs/pi5-dhcp-ack-port-verified-2026-11-11.txt)
passes the corrected first-M4/key ordering and peer-port setup. The firmware's
peer flags change from authenticated/associated to also authorized; the final
snapshot remains authorized. One DISCOVER and one REQUEST produce a valid OFFER
and ACK, with two successful decryptions and zero reported decryption failures.
Explicit deauthorization, DOWN, key deletion, policy restoration, storage and
page tests pass.

That run received address `10.21.217.100`, mask `255.255.255.0`, gateway/DNS
`10.21.217.61` and a 3599-second lease. **These are observed values, not hardcoded
configuration.** The bounded DHCP command still ends DOWN and installs no IP.

The next command is **`wifiping <SSID>`**. It performs the complete fresh
SAE/association/M3/M4/key/port/DHCP path, then runs address checks, gateway ARP
resolution and three ICMP echo requests while the verified connection remains
inside the command's bounded scope. The new ARP/ICMP path needs a physical Pi
test. It does not leave a persistent connection or an IP configured afterward.

## Behavior

1. Require the ACKed address, mask and first router to describe distinct usable
   hosts in one subnet. Missing router/mask, noncontiguous masks, subnet/
   broadcast addresses, off-link routers and /31 or /32 host layouts are
   unsupported. No guessed gateway or static fallback is used.
2. Before claiming the leased address, send three broadcast ARP probes with
   sender IP `0.0.0.0`. Use an initial random 0–1 second wait and randomized
   1–2 second gaps; wait another two seconds after the last probe. Receive
   checks detect another MAC claiming the candidate, or simultaneously probing
   it. No conflict observed during this window is a limited observation, not
   a guarantee that every device is reachable or the address will remain free.
3. Send two ARP announcements two seconds apart, then use the IPv4 address
   temporarily. While active, queue and answer valid ARP requests for this
   address outside callbacks. At most one reply is queued and eight are sent;
   excess traffic can stop this narrow test. Continue watching for conflicting
   claims. On a conflict, relinquish immediately instead of defending.
4. Resolve the gateway with up to three broadcast ARP requests, one second
   apart. Accept a peer-bound ARP reply only for our requested gateway/address
   and MAC. Learning the gateway is distinct from assuming the AP BSSID is its
   Ethernet address. Refuse a different gateway MAC during the test. ARP is
   not cryptographically authenticated; a hostile peer can still cause denial
   of service or poison address discovery on a shared LAN.
5. Send three IPv4 ICMP echo requests to the gateway, about one second apart,
   with a fresh random identifier/token. Verify the reply's Ethernet endpoints,
   IPv4 geometry/addresses/checksum, ICMP type/code/checksum, identifier,
   sequence and entire 32-byte payload. Count each sequence once, and only
   after its corresponding successful write. Ignore unmatched/duplicate
   replies. Wait up to two seconds after the last request. Success requires
   at least one verified reply and all three completed request writes;
   `reply_mask=7` means all three replied.
6. Remove the temporary address on every exit. Query final firmware peer
   state/counters, then explicitly deauthorize, DOWN, delete attempted keys,
   clear the RSN override, restore policy/events and erase private handshake
   state. Unconfirmed cleanup poisons further transport access. Existing M3/M4
   handling never reinstalls keys, and still enforces its original retry limits.

If a conflict is detected, attempt one broadcast **DHCPDECLINE** for the ACKed
address and server, then stop. DECLINE uses source IP zero, requested-address/
server/client-ID options and valid checksums; it contains no parameter-request
list. A failed write is reported, not converted to success. There is no automatic
address acquisition retry this boot. Allow at least ten seconds before a new
attempt. There is no DHCP RELEASE or lease renewal yet; the server may retain
a successful test lease until expiration.

The ARP/ICMP scope is bounded by 30 seconds elapsed and 3000 polls, plus bounded
SDIO/control I/O. DHCP has its separate 45-second bound. Lease validity is
conservatively measured from the start of DHCP acquisition, and rechecked after
transmit-credit handling before writes. A short/expired lease stops the test.
Transmit-credit deferrals are capped at eight. The existing 256-data-frame
association limit also remains; busy LAN traffic can exhaust this diagnostic
scope. No ARP/ICMP I/O or clock reads occur inside receive callbacks.

Original implementation: [packet/state engine](../network/netprobe.c),
[Wi-Fi integration](../drivers/storage/brcm_assoc.c),
[typed transport](../drivers/storage/brcm_control.c),
[DHCPDECLINE](../network/dhcp.c). Wire/timing references:
[ARP/RFC826](https://www.rfc-editor.org/rfc/rfc826.html),
[address checking/RFC5227](https://www.rfc-editor.org/rfc/rfc5227.html),
[ICMP/RFC792](https://www.rfc-editor.org/rfc/rfc792.html),
[DHCP/RFC2131](https://www.rfc-editor.org/rfc/rfc2131.html).

## Image

Complete disk: **134217728 bytes** (128 MiB); ZIP: **782083 bytes**;
kernel `OUROS.IMG`: **314372 bytes**. Full-disk SHA256:
`8a2a4f17bee96da081e703a1035e14f504416d2941c1eb11074557d516994f6f`.
Previous complete files are preserved flat in `image_files` as
`OurOS-Pi5-header-experimental-sd-20261111-211356-573.img` and
`OurOS-Pi5-header-experimental-sd-20261111-211402-440.zip`.
Kernel/BSS/stack ends at `0x1ff000`, below the firmware DTB at `0x200000`.
The linker/image-header checks guard that boundary. Future kernel growth will
need a revised layout; this image fits the current reserved region.

## Pi procedure

Extract the complete [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to your existing USB pendrive, or flash the local
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). Power-cycle the Pi.
Use the CP2102/header console at 115200 8N1, no flow control; force PuTTY Local
echo and Local line editing off.

On a fresh boot run separately, waiting for each prompt:

```text
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
wifiping Jack's
netinfo
wifiinfo
storagetest
pagetest
uptime
```

Enter the password only at the hidden local prompt. Skip `wifidhcp`, `wifiassoc`
and the other join/SAE probes in this boot; choose `wifiping` directly after
target checking. Reboot before retrying. The long P256/H2E/SAE public math tests
are optional. `nettest` now checks both DHCP and ARP/ICMP public packet fixtures
without network I/O.

Intended success markers, pending hardware verification:

```text
Association/EAPOL probe: ok phase=11
Controlled port: ok phase=4 AUTHORIZE_SET=acknowledged readback=verified
DHCP probe: ok phase=3
Gateway probe: ok phase=5
ARP checks: probes=3 announcements=2 conflict=not-observed temporary_IP_used=yes active_now=no
Gateway ARP requests=... learned=yes MAC=... changed=no
ICMP to gateway ... sent=3 reply_mask=...7
echo sequence=1 observed_latency_ms=...
DEAUTHORIZE_SET=acknowledged
```

`observed_latency_ms` includes polling, SDIO/control processing and any M3
service; it is not a precise wire RTT. Partial reply masks can still produce
success, as described above. Share the complete `wifiping` output, `netinfo`,
`wifiinfo` and the final storage/page/uptime checks. Include setup output if
anything fails before the password prompt. Do not include the password.

**A gateway reply proves local IPv4 reachability during this test.** External
numeric-address reachability, DNS queries, a persistent supplicant/group rekey,
lease renewal and TCP/TLS/package installation are separate future stages.
`ping www.google.com` is not available yet. This implementation accepts a
fixed IPv4 header with no options/fragments and handles echo replies only;
ICMP error decoding, inbound echo responses and a general packet/socket API
remain unimplemented.

## Validation

Independent Node fixtures cover exact ARP probe/announcement/resolution/reply
and IPv4/ICMP request/reply bytes for eight public parameter sets. Standalone
tests check timing, pre/post-activation conflicts, gateway binding, ARP reply
queuing, checksums, identifiers/tokens/sequences, early replies followed by
failed writes, missing replies, lease expiry, stopped clocks, credit bounds,
every model-I/O failure and 10000 receive mutations with output guards.
Separate DHCP fixtures verify all 16 exact DECLINE packets and the ACK/write
gate. The complete Wi-Fi model covers SAE through gateway replies and cleanup,
including conflicts/DECLINE, no router, RNG/write/deauth failures and M3 during
network credit waits without key reinstallation. Actual CDC/SDPCM transport
tests inspect 76-byte ARP and 92-byte echo writes and guard cancellation.
These tests do not establish physical ARP/ICMP delivery; the new Pi run does.

Completed validation for this image:

- Full native Wi-Fi/SDIO/crypto suite: `build/wifi-tests/phase-gateway-suite.log`.
- Cortex-A53/A76 monitor/storage, public DHCP/ARP/ICMP packet fixtures,
  missing-Pi-radio refusal and fatal-exception cases:
  `build/phase-gateway-qemu.log`; delivered disk hash unchanged.
- USB2 BOT/FAT/firmware files, all ring wraps and EL2-to-EL1 handoff:
  `build/phase-gateway-usb-qemu.log`; delivered disk hash unchanged.
- Corrupted MBR/FAT/kernel and console-profile mismatch rejection:
  `build/phase-gateway-image-negative.log`.
- ZIP integrity and independent extracted full-disk SHA256 verification.

Previous full images/ZIPs are preserved and known generated build outputs are
cleaned after validation. No physical drive was written and no Git commit or
push was performed; the user handles publication.
