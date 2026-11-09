# External-SAE transport and reversible firmware policy check

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

The [new capture](../proofs/pi5-auth-mask-blocked-2026-11-09.txt) again passes
the math tests, USB reads and passive scan (Jack's, channel 6, -44 dBm). However,
`wifiauthprep` stops at phase 2 / `event_msgs_ext` with firmware status zero.
The driver rejected the reply layout before attempting any SET. The old
diagnostics did not retain its header, so the exact incompatibility is unknown.
This revision expands the previous 32-byte mask buffer to the full 255-byte
range represented by the ABI and prints reply length/version/command/length/
maxgetsize. This can handle a larger mask without discarding unrelated bits;
it does not assume that a larger mask is the proven cause. Invalid or truncated
replies still stop before any SET. Physical confirmation is pending.

## Test on the Pi

Flash `image_files/OurOS-Pi5-header-experimental-sd.img` onto the same pendrive.
Complete size: **134217728 bytes / 128 MiB**. Raw kernel: 198532 bytes.
SHA256: `fc67fcb4eceb9ce3fdd308aca11f14be92ffccc23708b95dacb551ac09075da4`.
The GitHub [complete-image ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
is **727914 bytes**. Extract the `.img` before flashing. Its decompressed hash
is verified against the full disk by the build.

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
auth=0x0000000000000003 wpa_auth=0x0000000000040000 wsec=0x0000000000000004 mfp=0x0000000000000002
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
Restoration writes wsec before mfp because wsec may reset the PMF setting.
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
at each of 25 operations. They also exercise unsupported/short masks, poisoned
transport, readback mismatch and cached refusal. Complete mask restoration is
checked at lengths 24, 25, 32, 33, 64, 128 and 255, including unrelated high bits.
IOVAR tests transport the
extended GET request, policy SETs and management-frame SET through the actual
CDC/SDPCM framing implementation. Run `scripts/test-brcm-auth.ps1` or the full
`scripts/test-wifi.ps1` suite. These use simulated I/O and prove no physical
firmware support by themselves.

The final image also passes AArch64 QEMU monitor tests on Cortex-A53/A76,
EL2-to-EL1 USB/FAT checks, image validation and corruption rejection. QEMU
explicitly refuses `wifiauthprep` because it has no Pi Wi-Fi radio; the physical
firmware policy check remains unverified until the new UART capture.

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

After the physical policy check, connect event delivery and management TX to
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
