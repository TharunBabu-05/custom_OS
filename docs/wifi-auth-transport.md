# External-SAE transport and reversible firmware policy check

**Current hardware test: [association/EAPOL observation](wifi-association.md).**
The newest [Pi capture](../proofs/pi5-sae-confirm-verified-2026-11-10.txt)
passes live SAE peer confirmation. The account below describes earlier stages.
The [new commit capture](../proofs/pi5-sae-rx-metadata-v1-2026-11-10.txt)
passes TX ACK and reaches RX metadata v1. The current decoder handles that
header; the new confirm command checks the peer HMAC and then tears down.
The [latest capture](../proofs/pi5-external-sae-request-verified-2026-11-10.txt)
now passes station initialization, matched external-SAE request delivery,
DOWN and restoration. The new phase adds hidden local credential input and a
single commit/reply observation; confirms, association, keys and IP remain.
The following describes the earlier policy/request stage.
The 2026-11-10 capture stops at initial INFRA=0, before policy/UP/join.
This build initializes an idle non-AP interface to INFRA=1 with verified
readback, then continues the probe. The station selection remains for this
boot; security/event snapshots and restoration follow that initialization.
The latest capture confirms the temporary event mask and restoration, but
auth reads 2 immediately after its SET and wsec reports 0x604. The latter is
AES plus PMF flags; this revision fixes that comparison. The ordering change
did not solve the auth readback. The strict `wifiauthprep` check can still
report auth=2; the new request probe uses firmware's matched external-SAE
request to establish the host-authentication path, without supplying secrets
or completing authentication. Use the new guide's sequence for this phase.

The [SAE Pi capture](../proofs/pi5-sae-verified-2026-11-09.txt) passes
`saetest`, H2E, P-256, HMAC/HKDF and the RNG check. USB/FAT, storage and pages
still pass. Jack's appears on channel 6 / 2.4 GHz at -46 dBm with ordinary SAE,
required PMF and H2E advertised. This tested image is preserved as
`image_files/OurOS-Pi5-header-sae-verified.img`.

This next revision adds an external-authentication frame codec and event
parser, plus `wifiauthprep <SSID>`. The command checks firmware policy support
while the interface is DOWN, then restores the original settings. It does not
transmit authentication frames, raise the interface, join the hotspot or accept
a password. The codec is not yet connected to a live association state machine.

The [latest capture](../proofs/pi5-auth-normalization-2026-11-09.txt) passes
USB reads, firmware startup, scanning and target preflight (Jack's, channel 6,
-24 dBm). The temporary extended-event mask, legacy cross-check and stable
snapshots verify, as does full original policy/mask restoration. Storage and
page tests still pass. The complete tuple is auth=2, wpa_auth=0x40000,
wsec=0x604, mfp=2. Auth already reads 2 immediately after its SET, so the
ordering hypothesis did not resolve the getter discrepancy.

Wsec is a bitmask: 0x604 combines AES=4 with MFP_CAPABLE=0x200 and
MFP_REQUIRED=0x400. This revision corrects the literal wsec comparison:
AES alone or AES with both PMF flags is accepted, with separate strict mfp=2.
Other/unknown cipher bits or partial PMF flags fail. The standalone
`wifiauthprep` diagnostic still requires auth=3 and may therefore report one
remaining auth mismatch. No join or credentials are enabled by that command.

The new `wifijoinprobe` instead observes a matched firmware external-SAE
request under SAE-only WPA/AES/required-PMF controls, including a live recheck
after UP. Auth=2 is allowed only for this bounded observation and is not
declared to mean SAE. The probe then brings the interface DOWN and restores
policy/mask. Read [the new guide](wifi-join-probe.md) for the active sequence.
No cryptographic authentication, key installation or IP connection is claimed.

The [preceding capture](../proofs/pi5-auth-mask-header-2026-11-09.txt) exposed
the firmware reply layout responsible for the earlier parser rejection:

```text
Extended mask reply_bytes=259 version=101 command=118 length=24 maxgetsize=24
```

The first two bytes are ASCII `e` and `v`, matching the beginning of the
`event_msgs_ext` request name. A retained request prefix is the likely
explanation; the firmware implementation is not available to verify that
mechanism. The reported mask is only 24 bytes, so buffer capacity was not
the blocker. Our earlier requirement that the returned version equal 1 was
too strict for this observed response.

This revision accepts a normal v1 reply or exactly the captured `ev/24/24`
header, with all advertised mask bytes present. It rejects other unknown
versions and never treats a plain echoed IOVAR name as a mask. Every extended
read is checked against the independently working 16-byte `event_msgs` GET.
Two complete, identical snapshots are required before any SET. Readback and
restoration still compare every advertised byte. All SET requests construct
a fresh version-1 header; the `e/v` bytes are never sent as a SET header.
Snapshot compatibility, temporary enabled-event readback and exact restoration
are now physically confirmed.

## Test on the Pi

Flash `image_files/OurOS-Pi5-header-experimental-sd.img` onto the same pendrive.
Complete size: **134217728 bytes / 128 MiB**. Raw kernel: 207308 bytes.
SHA256: `c53ebeba017aa8c6ccb4a084f632d27e4f6cabddf5faa317d0d5255fc29a43c5`.
The local [complete-image ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
is **733356 bytes**. Extract the `.img` before flashing. Its decompressed hash
is verified against the full disk by the build.
Per the current preference, this revision is kept local; the user handles
GitHub publication until connection to Jack's is working.

On a fresh boot, enter one command per line and wait for each prompt:

```text
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
wifiauthprep Jack's
wifiinfo
storagetest
pagetest
uptime
```

Use uppercase `IN`, spell `wifiprepare` exactly, and omit surrounding quotes
around the SSID. The earlier log's `In` and `wifipreapre` were command-entry
errors; the corrected command succeeded.

A successful new check reports:

```text
Firmware auth-policy check: ok phase=6 operation=event_msgs_ext
Temporary SAE/AES/required-PMF and extended events verified.
...
Mask header compatibility=captured ev/24/24 legacy_crosscheck=verified initial_snapshot=stable
...
auth=0x0000000000000003 wpa_auth=0x0000000000040000 wsec=0x0000000000000604 mfp=0x0000000000000002
...
Expected: auth=3 wpa_auth=0x40000 wsec=AES(4 or 0x604 with PMF) mfp=2; mismatch_bits=0x0000000000000000
...
Temporary extended-event mask readback=verified
...
Original security policy and complete event mask restored and verified.
Interface remains DOWN. No join, credential, management-frame send or network connection.
```

The event-mask length is read from firmware; it must cover event 190 and fit
the 255-byte ABI bound, with every advertised byte present. The test is one-shot. Further invocations
display cached status; reboot to repeat. Paste the complete new command output
and the final regression checks, including failure output if it does not pass.

If snapshot queries are unsupported, no settings are changed. After any
attempted SET, the check attempts to restore every saved field and the entire
saved mask, then reads them all back. If restoration cannot be verified, the
control path is marked unavailable; reboot before further Wi-Fi work. There
is no fallback to WPA2 or optional PMF. Physical execution of this new check
is awaiting a capture.

## Implemented

The existing scan mask covered 128 event IDs. External authentication requires
IDs **187/188** for request/RX and **189/190** for TX status/off-channel completion.
The new check queries `event_msgs_ext` version 1, preserves all returned mask
bytes, temporarily enables these four events and verifies the readback.
The mask is restored in full, including unrelated bits beyond the old scan mask.

Security settings are snapshotted first. The temporary tuple is `auth=3`,
`wpa_auth=0x40000` (ordinary SAE), `wsec=4` (AES) and `mfp=2` (required).
The wsec readback may additionally include both PMF bits (0x604).
Restoration writes wsec before mfp because wsec may reset the PMF setting.
It now also restores wpa_auth before auth. Checkpoint labels are
`auth_after_wpa_auth`, `auth_after_auth`, `auth_after_wsec`, `auth_after_mfp`.
Every available final value is displayed even when another value mismatches;
unread fields still say `not-read`. A policy mismatch is labelled explicitly,
so a matching firmware acknowledgement is not misreported as an SDIO fault.
Diagnostic values are printed only when actually read; otherwise they say
`not-read`. The current clean scan and target/capability checks are required
before firmware access. The driver also requires the selected target to
advertise H2E, matching the implemented crypto profile.

The frame codec serializes `mgmt_frame` v0 with explicit ABI padding, a nonzero
packet ID, destination/BSSID, channel and the six-byte SAE authentication
header. Commit uses H2E status 126; confirm uses status 0. Only the existing
98-byte commit and 34-byte confirm bodies are supported. A restricted
`mgmt_frame` IOVAR transport path is available internally, but no monitor
command sends it yet.

The parser checks BCDC/event framing, interface/BSS index and bounded lengths.
Requests are matched by payload BSSID/SSID. RX is matched to the event's peer
address and decoded control channel; the firmware-supplied 24-byte MAC header
is not treated as authoritative because the reference reconstructs it.
The initial RX metadata version is narrowly gated to version 0 and awaits
physical event evidence. TX events are matched by packet ID; TX completion,
no-ACK and off-channel completion remain distinct. Parsing an event is not an
ACK or authentication decision. Unsupported SAE statuses/extensions remain
unsupported; no crypto session is advanced by this codec alone.

Host tests verify exact bytes, lengths, padding and endianness; request/RX/TX
filters; 5000 mutated event packets; and policy/mask restoration under failures
at each of 34 operations for both normal and compatibility replies (68 injected
failures). They also exercise unsupported/short masks, poisoned transport,
legacy mismatch/short replies, unstable high mask bits, plain request echoes,
near-miss compatibility headers, readback mismatch and cached refusal. Complete mask restoration is
checked at lengths 24, 25, 32, 33, 64, 128 and 255, including unrelated high bits.
IOVAR tests transport the
extended GET request, policy SETs and management-frame SET through the actual
CDC/SDPCM framing implementation. An integrated firmware model reproduces the
captured 274-byte padded CDC reply with `e/v` at offsets 0/1 and mask at offset
4; it runs all 34 real transport exchanges and verifies exact restoration.
The model also includes a WPA-mode transition resetting auth to AUTO, so it
would fail with the previous apply/restore order. This reset is a modelled
possibility, not captured firmware behavior. Tests reject persistent auth=2,
an extra WPA2-PSK flag and optional PMF; retain all four readbacks and the first
failed operation; and verify non-default profile restoration and preservation
of the first firmware restoration error despite later successful reads.
Only the header/padded length are captured evidence; the model's mask bytes
and policy values are test fixtures. Run `scripts/test-brcm-auth.ps1` or the full
`scripts/test-wifi.ps1` suite. These use simulated I/O and prove no physical
firmware support by themselves.

Validation for this revision includes the full native Wi-Fi/crypto suite,
the Pi AArch64 build, package/disk checks, corruption rejection, ZIP integrity
and EL2-to-EL1 QEMU USB/FAT reads with an unchanged source disk hash. QEMU
explicitly refuses `wifiauthprep` because it has no Pi Wi-Fi radio. These
checks cannot establish the new setting order's behavior on physical firmware.
The prior Cortex-A53/A76 monitor/math tests cover unchanged monitor paths.

## Remaining integration

### Long self-tests

`p256test`, `h2etest` and `saetest` use fixed-loop software field arithmetic.
Each field product currently reduces all 512 bits; the Pi runs with MMU and
caches off. A first run can therefore take minutes. These tests are optional
for the event-mask check above and need not be repeated in every UART capture.
No arithmetic validation or RNG requirements were weakened to reduce waiting.

Each command now retains its PASS or FAIL result until reboot and reports
elapsed seconds on an actual run. Repeating it displays a clearly labelled
cached result immediately. Use `p256test force`, `h2etest force` or
`saetest force` to run it again. Reboot clears the cache. Full native vector
tests and the QEMU regression still execute all three suites.

### After the policy check

After the physical request probe, connect event delivery and management TX to
the SAE engine with peer/status/sequence validation, deadlines, ACK handling,
anti-clogging/retry behavior and reviewed protected credential/RNG plumbing.
Then implement association, RSN/RSNXE handling, EAPOL and key installation with
PMF. ARP/IPv4/DHCP, ICMP and DNS follow a protected link. No internet connection
or package installation is available yet.

Primary interface references used for the original implementation:
[authentication TX/RX and policy configuration](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c),
[management-frame and RX metadata layouts](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[extended mask and event IDs](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fweh.h),
[primary-BSS IOVAR framing](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.c).

The Broadcom reference [DHD event-mask GET/SET sequence](https://android.googlesource.com/kernel/common/%2B/bcmdhd-3.10/drivers/net/wireless/bcmdhd/dhd_linux.c#6034)
uses the returned mask without requiring the GET's version/command fields to
be echoed, then explicitly reconstructs version/command for SET. That supports
avoiding an echo requirement; it does not document the specific `e/v` firmware
behavior inferred from this Pi capture.
