# Wi-Fi data port and DHCP reply correction

**This phase passes on the Pi:** [physical capture](../proofs/pi5-dhcp-ack-port-verified-2026-11-11.txt).
Authorization readback, DHCP OFFER/ACK, two successful decryptions and cleanup
pass. The current phase is [address checks, ARP and gateway ping](wifi-gateway.md).
The details below record the preceding correction.

The [latest physical transcript](../proofs/pi5-dhcp-discover-no-rx-2026-11-11.txt)
passes SAE confirmation, association identity/security, authenticated M3 key
data, three higher-counter M3 retries, three M4 FIFO writes and clean teardown.
USB storage and page tests also pass. **DHCP did not pass:** three DISCOVER
writes completed, but zero receive frames reached DHCP, no OFFER was observed,
and no REQUEST was sent. This is a data-path stop after SAE, not evidence that
the supplied password was wrong. No working Internet connection is established.

The previous tests proved bus writes and firmware acknowledgements. They did
not prove AP acceptance of M4 or delivery of encrypted network packets. This
revision corrects two implementation gaps; their contribution to the physical
failure remains an inference until a new Pi run verifies the changed path.

## Corrections

1. After validating SAE, association and M3 MIC/key data, send the first M4
   **before installing the transmit TK**. A completed SDIO write is the bus
   ordering boundary; it is not an air ACK. Then install PTK/GTK/IGTK once,
   verify identity/security and service authenticated M3 retries without
   reinstalling keys or resetting counters. A failed initial M4 prevents key
   installation. Partial installation cannot permit another M4.
2. For `wifidhcp`, query the selected AP's `sta_info` before DHCP. Validate the
   declared length, supported version and exact peer MAC; require authenticated
   and associated flags. Issue peer-specific `SET_SCB_AUTHORIZE` (121), then
   read back the same peer's authenticated/associated/**authorized** flags.
   An acknowledged SET alone cannot enable DHCP. Unsupported/unknown replies
   fail with diagnostics instead of bypassing the readback gate.
3. Recheck permission after credit pumping and immediately before authorization
   or DHCP writes. An M3 arriving during authorization-credit processing defers
   authorization until the outside-callback M4 service handles it. There are
   at most eight authorization attempts. DHCP writes additionally require a
   verified peer-port snapshot and all existing handshake/key/identity gates.
4. Read final peer state/counters after the DHCP test, including a DHCP timeout
   when transport still permits queries. Loss of authorization or a failed
   final query fails an otherwise successful test. The first DHCP error is
   preserved. These are snapshots, not continuous link monitoring.
5. Explicitly send peer-specific `SET_SCB_DEAUTHORIZE` (122) for every attempted
   authorization, including a rejected or uncertain SET. Then still attempt
   DOWN, key deletion and policy restoration. An unconfirmed deauthorization
   poisons further transport access even if later cleanup succeeds. Cleanup
   never depends on a still-valid credential/session or an open data port.

The implementation is original. The ordinary hostap
[M3 handler](https://raw.githubusercontent.com/vanhoefm/hostap-wpa3/master/src/rsn_supp/wpa.c)
sends M4 before enabling the transmit PTK. The Broadcom driver
[waits for pending host EAPOL transmission before key configuration and exposes peer authorization](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c).
The firmware [command IDs](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.h)
and [station-info layout/flags](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h)
define the wire interface. Our bus-completion checks do not claim an over-air
acknowledgement or independent certification of the handshake implementation.

## Image

Complete disk: **134217728 bytes** (128 MiB); ZIP: **775650 bytes**;
kernel `OUROS.IMG`: **301812 bytes**. Full-disk SHA256:
`123c828ab5f956ec57830e418427503aeef4b58c310658c0ea4c0d392efb6218`.
The previous DHCP image and ZIP were preserved flat in `image_files` as
`OurOS-Pi5-header-experimental-sd-20261111-203853-617.img` and
`OurOS-Pi5-header-experimental-sd-20261111-203856-500.zip`.
Kernel/BSS/stack ends at `0x1fc000`, below the firmware DTB at `0x200000`.
The linker/image-header checks guard this boundary. The corrected radio path
remains hardware untested.

## Run on the Pi

Extract the new complete [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the existing USB pendrive. The flat local
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img) is also available.
Power-cycle the Pi. Keep the CP2102/header console at 115200 8N1, no flow control;
force PuTTY Local echo and Local line editing off.

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
wifidhcp Jack's
netinfo
wifiinfo
storagetest
pagetest
uptime
```

Enter the password only at the hidden prompt. Skip other join/SAE probes and
`wifiassoc` in this boot. Reboot before retrying. The long P256/H2E/SAE public
self-tests are optional for this run. `wifiassoc` retains its bounded
handshake-only purpose and uses the corrected first-M4/key ordering too.

Intended markers, pending physical verification:

```text
First M4 before TX key installation=yes
Controlled port: ok phase=4 AUTHORIZE_SET=acknowledged readback=verified
SCB before valid=yes ... authenticated=yes associated=yes ...
SCB after valid=yes ... authenticated=yes associated=yes authorized=yes
SCB final query: ok
SCB final valid=yes ... authorized=yes
DHCP probe: ok phase=3
DISCOVER_writes=... REQUEST_writes=... OFFER=observed ACK=verified for transaction/server
DEAUTHORIZE_SET=acknowledged
```

`SCB counters` appears only when the supported reply declares at least 84 bytes
and advertises the per-station statistics flag. It reports absolute TX/failure,
unicast/multicast RX and successful/failed decryption counters. They can include
other traffic and are not per-DHCP-packet acknowledgements. Comparing the three
snapshots can distinguish some TX/RX/decryption failures. When statistics are
unavailable, the state/length/flags remain visible. Supported metadata versions
are 3–7, with a stable peer/state prefix of at least 22 declared bytes, bounded
by the 512-byte query buffer. Unknown versions are reported without trusting
their layout. Raw frame bodies, credentials and keys are never printed.

If no reply arrives, `DHCP probe: reply deadline expired` replaces the generic
controller-timeout description. Capture the complete `wifidhcp` output,
`netinfo`, `wifiinfo` and storage/page/uptime results. Also include setup output
if the failure occurs before the password prompt. Do not include the password.

This is still a bounded test that ends deauthorized/DOWN. No IPv4 address is
installed, and ARP conflict detection, routing, lease renewal, persistent
supplicant/group rekey, DNS queries, ping and package installation remain later
work. DHCP's packet filtering, retries and other limits are documented in the
[preceding implementation](wifi-dhcp.md#packet-and-state-boundaries).

## Validation

The native full-handshake model checks initial M4 before key installation,
authorization/readback before DHCP, rejection, malformed or wrong-peer state,
missing authentication/authorization, loss of authorization, credit-time M3
refresh without key reinstallation, deauthorization failure and every operation
failure in a complete successful path. It retains private-state erasure and
DOWN/key-deletion/policy-restoration checks.

`tests/brcm_port_host.c` uses an independent native ABI layout and the actual
CDC/SDPCM implementation to inspect query and 121/122 SET bytes. It checks
declared lengths, all prefix truncations, supported/unknown versions, peer
binding, counter length/flag gates, SET length echoes, firmware rejection,
credit-time guard cancellation and every bus-I/O failure. No arbitrary firmware
command API was introduced. Emulator tests cannot verify the physical radio or
hotspot response; a new Pi transcript is required.

Completed checks for this revision:

- Full native Wi-Fi/SDIO/crypto suite: `build/wifi-tests/phase-port-suite.log`.
  The complete port/DHCP failure model covers 368 operation boundaries.
- Cortex-A53/A76 monitor/storage and fatal-exception cases:
  `build/phase-port-qemu.log`; delivered disk hash unchanged.
- USB2 BOT, FAT32/firmware files, all ring wraps and EL2-to-EL1 handoff:
  `build/phase-port-usb-qemu.log`; delivered disk hash unchanged.
- Corrupted MBR/FAT/kernel and console-profile mismatch rejection:
  `build/phase-port-image-negative.log`.
- ZIP integrity and independent full-disk SHA256 after extraction.

Previous complete images/ZIPs are preserved, and known generated build outputs
are cleaned after validation. No physical drive was written and no Git commit
or push was performed; publication remains with the user.
