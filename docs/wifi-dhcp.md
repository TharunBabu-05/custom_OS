# DHCP over the verified WPA3 handshake

**DHCP now passes on the Pi.** Follow the current
[foreground-session guide](wifi-session.md) for the next test.

The [peer-port correction](wifi-data-port.md) resolved the earlier no-reply stop:
first-M4/key ordering, authorization/readback and DHCP now have physical Pi
evidence. The details and artifact sizes below record the original DHCP revision.

The [latest Pi capture](../proofs/pi5-m3-retry-m4-verified-2026-11-11.txt)
passes the previous phase: three authenticated higher-counter M3 retries,
three completed M4 writes, post-M4 identity verification, no key reinstallation,
and acknowledged key deletion/normal cleanup. Storage and page tests also pass.
That capture proves the bounded handshake test; it contains no DHCP traffic.

This revision adds `wifidhcp <SSID>`, a bounded DHCP DORA test after the same
verified SAE/M3/key-install/M4 path. The firmware handles CCMP using those keys;
OurOS builds Ethernet/IPv4/UDP/DHCP packets and processes replies. **The new DHCP
path needs physical Pi verification.** A completed FIFO write alone does not
prove AP acceptance. A matching DHCP response provides evidence of data delivery.

The test sends DISCOVER, selects the first valid OFFER, broadcasts REQUEST,
and validates the selected server's ACK. It reports the offered/acknowledged
IPv4 address, server, lease duration and, when supplied, mask, gateway and first
DNS server. It then brings Wi-Fi DOWN, deletes attempted keys, clears the RSN
override, restores policy/events and erases private handshake material.

That revision's complete disk is **134217728 bytes** (128 MiB), the ZIP is
**772992 bytes**, and `OUROS.IMG` is **297540 bytes**. Disk SHA256:
`0da110fc3bf70701ce82e5ba6b430495de1a757dfff4d2343778f1f84eb8b598`.
The previous complete disk/ZIP are retained as
`OurOS-Pi5-header-experimental-sd-20261111-200625-579.img` and
`OurOS-Pi5-header-experimental-sd-20261111-200630-217.zip` in `image_files`.
Kernel/BSS/stack ends at `0x1fb000`, below the firmware DTB at `0x200000`;
the linker and image-header checks guard that boundary.

**An observed ACK is not an installed IP address.** This phase does not run ARP
address-conflict detection, configure an interface/route, keep the connection
alive, renew a lease, query DNS or implement ping. `ping www.google.com` and
package installation remain future work. No DHCP RELEASE is sent on teardown;
the server may retain its lease until expiration. `netinfo` displays cached
public metadata, including any partial OFFER/ACK, even if a later check fails.
Always read the overall association result and cleanup status too.

## Run on the Pi

Extract the complete [disk ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to your existing USB pendrive. The raw
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img) is also available locally.
Power-cycle the Pi. Use the existing CP2102/header console, 115200 8N1, no flow
control, with PuTTY Local echo and Local line editing forced off.

On a **fresh boot**, run separately and wait for each prompt:

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
wifidhcp Jack's
netinfo
wifiinfo
storagetest
pagetest
uptime
```

Enter the hotspot password only at the hidden local prompt. It is not saved or
embedded. Skip `wifiassoc` and the other join/SAE probes in this boot: they are
one-shot and finish DOWN. Reboot before retrying. The long P256/H2E/SAE self-tests
are optional; `nettest` uses public packet fixtures with no network I/O.

Expected success markers, **pending a new hardware test**:

```text
Association/EAPOL probe: ok phase=10
Post-M4 identity=verified
DHCP probe: ok phase=3
DISCOVER_writes=... REQUEST_writes=... OFFER=observed ACK=verified for transaction/server
ACK address=... server=... lease_seconds=...
mask=... gateway=... DNS=...
Firmware key cleanup=acknowledged clear_mask=...7
private_keys_erased=yes
Probe ended DOWN.
```

Share the complete output from `wifidhcp Jack's` through `netinfo`, `wifiinfo`
and the final storage/page/uptime checks. The earlier setup output is useful if
anything fails before the password prompt. Do not include the password.

## Packet and state boundaries

- Fresh hardware RNG supplies a nonzero transaction ID and delays; no timer/MAC
  entropy fallback. Initial delay is 1–10 seconds, followed by approximately
  4 ± 1 and 8 ± 1 second retry intervals. At most three completed transmissions
  per stage, eight credit deferrals, 4500 polls and 45 seconds elapsed are
  allowed, plus bounded SDIO/control I/O. Wait for the prompt.
- Receive callbacks validate/copy only. They do not transmit or call I/O.
  A typed transmit entry point accepts only the exact DHCP DISCOVER/REQUEST
  layout, with valid IPv4/UDP checksums; there is no arbitrary IPv4 transmit API.
- Replies require Ethernet destination/local-client identity, BOOTP geometry,
  transaction ID, magic cookie, valid lengths/checksums, direct server identity,
  valid options and a matching selected-server/address ACK. A returned client
  identifier must match. Duplicate critical options, overload, fragments,
  IP options and relayed replies are unsupported. An IPv4 UDP zero checksum
  is legal; nonzero checksums must verify. DHCP itself is not cryptographic
  server authentication. The handshake gates the Wi-Fi path separately.
  Subnet masks must be contiguous /1 through /31; router/DNS lists are limited
  to eight addresses and only the first is retained.
- Missing mask/router/DNS is reported as `not-supplied`; a valid lease and server
  are required. Invalid/unrelated packets are counted and ignored. A matching
  NAK, timeout, disconnect, I/O error or failed final identity check fails the test.
- An OFFER or ACK arriving during transmit-credit handling can be recorded, but
  success also requires the corresponding successful write. No received reply
  converts a failed write into success. No IP address is installed on any path.
- The existing M3 retry/M4 service runs outside callbacks during DHCP polling.
  A credit-time M3 invalidates permission to write DHCP until its authenticated
  M4 response completes. The original limits remain: three higher-replay
  candidates, eight exact duplicates and four M4 writes. Group rekey is not
  implemented. The association RX cap remains 256 data frames; a busy network
  can exhaust it and stop this diagnostic test.

Original implementation: [DHCP](../network/dhcp.c),
[association integration](../drivers/storage/brcm_assoc.c),
[typed SDPCM transport](../drivers/storage/brcm_control.c).
Wire references: [DHCP/RFC2131](https://www.rfc-editor.org/rfc/rfc2131.html),
[options/RFC2132](https://www.rfc-editor.org/rfc/rfc2132.html),
[UDP/RFC768](https://www.rfc-editor.org/rfc/rfc768.html),
[IPv4/RFC791](https://www.rfc-editor.org/rfc/rfc791.html).

## Validation

`scripts/test-dhcp.ps1` independently generates 16 full DORA fixture sets in
Node and checks exact C client packets, legal zero/nonzero server checksums,
truncations, client/server/transaction filtering, malformed/duplicate options,
NAKs, early replies followed by failed writes, lost replies, stalled clocks,
bounded retries/deferrals, output guards and 10000 receive mutations.

The full native Wi-Fi suite additionally models SAE through DHCP and cleanup:
lost OFFER/ACK, NAK, RNG failure, disconnect, final identity failure and M3
during DHCP credit waiting. Typed transport tests inspect the actual
360-byte SDPCM/BCDC write and cancellation after RX. Emulator checks exercise
public packet fixtures, UART, filesystems, unchanged read-only disk hashes and
the explicit refusal to use a nonexistent Pi radio. Models/QEMU do not prove
encrypted over-the-air delivery or DHCP behavior on the physical hotspot.

Completed for this revision:

- Full native Wi-Fi/crypto/SDIO suite: `build/wifi-tests/phase-dhcp-suite.log`.
- Cortex-A53/A76 monitor/storage and fatal-exception checks:
  `build/phase-dhcp-qemu.log`; delivered image hash unchanged.
- USB2 BOT/FAT/files/firmware assets, all ring wraps and EL2-to-EL1 handoff:
  `build/phase-dhcp-usb-qemu.log`; delivered image hash unchanged.
- MBR/FAT/kernel corruption and console-profile mismatch rejection:
  `build/phase-dhcp-image-negative.log`.
- ZIP integrity and independent decompressed full-disk SHA256 verification.

No physical device was written by these build/tests. No Git commit or push was
performed; publication remains with the user.
