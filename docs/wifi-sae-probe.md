# First live SAE commit and peer-reply probe

**Current hardware result:** the [new capture](../proofs/pi5-sae-rx-metadata-v1-2026-11-10.txt)
passes commit SET/TX ACK but stops on RX metadata version 1. That decoder
restriction is fixed in the current image. Continue with the
[live SAE confirm test](wifi-sae-auth.md) on a fresh boot. The sections below
describe the original commit-only image and its procedure.

The [2026-11-10 Pi capture](../proofs/pi5-external-sae-request-verified-2026-11-10.txt)
passes station initialization, the exact-target external-SAE request,
interface DOWN and security/event-mask restoration. Jack's was on channel 11,
BSSID 92:b1:03:f4:e3:38. The driver always selects these from the current scan.
The capture also passes storage/page tests. It does not prove authentication.

The next local image adds **`wifisaeprobe <SSID>`**. It accepts a hidden local
password, prepares one fresh group-19 H2E commit before radio UP, then uses the
verified join scope to transmit that commit and observe TX status and a peer
commit. It brings the interface DOWN and restores security/events afterward.
**This new radio path is not yet tested on the physical Pi.**

A successful commit/reply probe does not prove the password is correct. There
is no SAE confirm exchange, authentication-success notification, association,
EAPOL/key installation, PMF data path, DHCP or IP yet. `ping` remains unavailable.
The next protocol boundary is the live SAE confirm exchange, followed by
association and key installation, then ARP/IPv4/DHCP/ICMP/DNS.

## Flash and run

Use the complete local image:

- `image_files/OurOS-Pi5-header-experimental-sd.img`
- Or extract that image from `image_files/OurOS-Pi5-header-experimental-sd.zip`.

It is a 128 MiB disk image for the existing USB pendrive/header-UART setup.
For this build the raw kernel is **216276 bytes**, the complete disk is
**134217728 bytes** and the ZIP is **737353 bytes**. Full-disk SHA256:
`3ccf122bb6e36d0101396afeec8156e790103dcf9fbf70aec56b3856fac7cecb`.
The image build validates the disk, preserves the previous full image and ZIP,
and checks the decompressed ZIP against the complete disk's SHA256. Nothing is
written to a physical drive, committed or pushed by these build scripts.

Start from a **fresh boot**. Do not run `wifijoinprobe` first: its candidate
SSID cache can remain in firmware after DOWN. The two probes deliberately
refuse reuse in the same boot. Run commands individually, waiting for each
prompt:

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
wifisaeprobe Jack's
```

Do not put quotes around the SSID. The current scan must uniquely identify a
2.4 GHz target on channels 1..13 with ordinary SAE, CCMP-128, required PMF and
H2E. Unsupported/ambiguous targets, unprepared control or failed entropy checks
stop before requesting a password. No WPA2 fallback is attempted.

At the **`Password (hidden, printable ASCII; ...)`** prompt, type the hotspot password directly
into PuTTY, without quotes, then press Enter. Do not send it back in chat or put
it on the command line. Configure PuTTY **Terminal > Local echo: Force off**
and **Local line editing: Force off**. The kernel sends no password characters
or masking stars back over UART. UART is not encrypted; local echo settings and
the connected PC/adapter still matter.

The reader accepts 1..128 printable ASCII bytes, including spaces and
punctuation. Backspace/Delete remove a character; Ctrl+U clears the line. An
empty Enter cancels. Ctrl+C marks the line cancelled; press Enter to finish.
Overflow or unsupported characters reject and erase the entire line; its tail
is consumed instead of becoming a monitor command. Ctrl+U clears an input
error and lets you retype before Enter. Only this prompt accepts a password;
there is no saved credential file.

The command automatically checks RNG200 and requires fresh hardware output;
there is no timer/MAC/deterministic fallback. It derives the H2E token, erases
the password, derives the MAC-bound password element and prepares a randomized
commit. It then erases the token, password element and private SAE session
before raising the interface. Only public commit bytes remain for transmission.

**This preparation can take minutes with the MMU/caches off.** Progress lines
and total preparation seconds are printed; wait for the next prompt. The
calculation happens before joining, so it does not consume the peer's live
authentication window. The already-verified `p256test`, `h2etest` and `saetest`
are optional; this sequence does not run them again. Do not type further
commands while the calculation is running.

The live request wait is bounded to 15 seconds/1500 polls; after transmitting,
the reply wait is bounded to 5 seconds/500 polls, plus bounded control I/O and
cleanup. There is exactly one commit transmission and no retries. Once valid
password input starts preparation, reboot before another attempt, even after
failure. Empty/invalid input cancellation before preparation can be retried.

After the command returns, run:

```text
wifiinfo
storagetest
pagetest
uptime
```

Paste the full **non-password** output from preparation onward and these final
checks. The useful markers, if the transport works, are:

```text
SAE preparation elapsed_seconds=... private state erased; commit ready.
SAE commit probe: ok phase=6 join_phase=7
request_seen=yes commit_TX_attempted=yes SET_ack=yes TX_status=0 ...
peer_RX=seen metadata_version=0 sequence=1 status=126 group=19 body_bytes=98 peer_commit_valid=yes reflection=no
Interface DOWN=acknowledged policy/mask restoration=verified transcript_buffers=erased
```

TX status 0 with the correct packet ID establishes a firmware-reported ACK;
off-channel completion alone is not an ACK. A peer commit still does not prove
that both sides derived the same password element. There is deliberately no
retained private session with which to continue this attempt to confirmation.

## Implementation and validation

The scoped policy/join path requires station mode, SAE-only wpa_auth, AES,
required PMF and the full event mask after UP. The measured legacy auth getter
value 2 is allowed only alongside those verified controls and a matching
external-SAE request. Neither a request nor a TX/RX event substitutes for a
successful SET acknowledgement. Cleanup always runs before returning.

The event callback does only bounded parsing/copying; it cannot perform I/O,
derive keys or run expensive curve operations. TX status is bound to nonzero
packet ID 1 for this single attempt. RX is bound to peer, interface and channel.
Peer scalar/point validation and reflection rejection run **after DOWN and
policy restoration**. Commit buffers are then erased. An unconfirmed DOWN or
restoration disables subsequent control I/O until reboot.

The RX subset now accepts metadata versions 0/1 and a fixed 98-byte group-19 H2E
commit. Anti-clogging requests (status 76), optional elements, other groups,
unexpected sequences/statuses and unknown RX metadata stop the probe and report
bounded diagnostics. They are not accepted as successful authentication.
There is no anti-clogging response or retransmission implementation yet.

Native tests cover early RX/TX events inside SET, out-of-order events,
duplicates, SET failure after events, NACK, off-channel-only completion, wrong
peer/channel/cookie, missing ACK/RX, stalled clocks, event floods, invalid
points/reflection, all 47 operations of the normal path failing individually,
DOWN-before-restore, poisoned control, one-shot refusal and buffer erasure.
Hidden-input tests cover no echo, CRLF boundaries, polling gaps, edits/clear,
cancel, length limits and rejection draining/wiping. Existing independent
OpenSSL and SAE/H2E vectors still exercise the arithmetic used by preparation.
Source-level fixed loops are not a compiler/CPU side-channel audit.

The full native Wi-Fi regression suite passes. Pi header, QEMU virt and
firmware-handoff builds pass; Cortex-A53/A76 monitor tests, the fault test and
EL2 USB BOT/FAT firmware-file/crypto checks pass. QEMU explicitly refuses the
new radio command without asking for a password. Disk validation, intentional
MBR/FAT/kernel/profile corruption rejection and 7-Zip integrity pass; emulator
reads leave the complete image hash unchanged. These results do not replace
the pending physical commit/reply capture.

Primary interfaces:
[Broadcom management-frame structures](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[CYW external-SAE event handling](https://code.googlesource.com/linux/torvalds/linux/%2B/23eb7901811a06805ec7d6e766dd82f083d03204/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cyw/core.c),
[nonzero TX packet ID fix](https://code.googlesource.com/linux/torvalds/linux/%2B/dab01c597f6bd40e0efe7da967b8374ca1971b79/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cyw/core.c),
[hostap SAE reference](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/sae.c).
