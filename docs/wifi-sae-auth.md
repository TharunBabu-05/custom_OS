# Live SAE confirm test

**Latest result: SAE authentication passed on the physical Pi.** The
[new capture](../proofs/pi5-sae-confirm-verified-2026-11-10.txt) reports
`peer_confirm_verified=yes`, both TX acknowledgements, 3-second preparation,
2900 ms peer key calculation and verified DOWN/restoration. It succeeded on
the first commit attempt. The current next step is
[`wifiassoc` association/EAPOL observation](wifi-association.md).
This page retains the earlier confirm-only procedure and artifact details.
The newest hotspot advertisement lacks H2E; current images also support
[SAE hunting-and-pecking](wifi-sae-hunt.md), with method-bound status checks.

## Earlier missing-reply investigation

The [earlier Pi capture](../proofs/pi5-sae-commit-ack-no-rx-2026-11-10.txt)
passes commit SET and firmware-reported TX ACK (`TX_status=0`) but reaches
`authentication timeout phase=3`: no peer RX was observed and no confirm was
sent. Password verification never ran. DOWN/restoration, storage and pages
pass. H2E/own-commit preparation now takes **3 seconds**, compared with the
preceding capture's 95 seconds after the field-reduction optimization.

This revision adds **up to three transmissions of the same commit**, each
with a distinct packet ID, after an ACKed transmission receives no matched
reply for five seconds. It also records early/filtered event diagnostics.
These changes address the observed missing-reply boundary; they do not prove
its cause or guarantee that the hotspot will respond. Hardware verification
has since passed for live confirms; retry recovery remains model-tested only.

The [preceding capture](../proofs/pi5-sae-rx-metadata-v1-2026-11-10.txt) did
deliver an RX event, rejected by the old version-0-only decoder. Its misleading
"sector outside device bounds" text came from a reused storage error string,
not a pendrive failure. That decoder restriction and wording are corrected.

This revision accepts the documented 16-byte version-1 metadata header while
keeping the legacy version-0 layout. Version 2 has a different header and is
still rejected. Length, interface, peer, channel, algorithm, sequence, H2E
status and group checks remain. The old capture contains no decoded peer body,
so whether that body is an ordinary commit or another response is still unknown.
[Linux metadata definition](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[vendor version-1/version-2 definitions](https://github.com/StreamUnlimited/broadcom-bcmdhd-4359/blob/master/include/bcmevent.h).

**`wifisaeauth <SSID>` now attempts live SAE commits and confirms.** If both
frame SETs and matching packet-ID TX ACKs succeed, and the peer's confirm HMAC
verifies, it reports `peer_confirm_verified=yes`. A valid commit alone cannot
produce that result. The latest physical capture now verifies this path.

This is an authentication test, not a complete connection command. It does
not export/install the PMK or notify firmware of authentication success.
It tears the interface down, restores security/events and destroys the
private session/keys, even on success. Association, EAPOL/PMF key installation
and a usable data path remain separate stages. The new `wifiassoc` command
adds a bounded association/M1 probe; see its guide. ARP/IPv4/DHCP,
ICMP/DNS and `ping` follow those steps. Knowing the password cannot bypass
those missing driver/protocol components.

## Run on the Pi

Flash `image_files/OurOS-Pi5-header-experimental-sd.img`, or extract the full
image from the matching ZIP. Older full images and ZIPs are preserved locally;
the build does not flash a physical device or commit/push to GitHub.
The earlier confirm-test raw kernel was **220852 bytes**, the full disk was **134217728 bytes**
(128 MiB), and the ZIP is **740597 bytes**. Full-disk SHA256:
`cbb5a1178dd7ad5a55b7b73ad6d9266874b9158b7a5af0e69d314d4cd9eb660a`.

On a **fresh boot**, keep Jack's hotspot enabled and run these commands one at
a time. Skip `wifijoinprobe` and `wifisaeprobe` in this boot: firmware can
retain a candidate SSID after DOWN, so these radio attempts are mutually
exclusive and one-shot.

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
wifisaeauth Jack's
```

Use the SSID without surrounding quotes. Set PuTTY **Local echo: Force off**
and **Local line editing: Force off**. Type the password only at the hidden
prompt, without quotes, then Enter. Do not put it in a command or share it in
the capture. Nothing is saved to a credential file or embedded in the image.
UART itself is not encrypted. Backspace/Delete edit, Ctrl+U clears, empty
Enter cancels. The existing 1..128 printable-ASCII limit and rejection
draining/wiping remain. No password input is requested on QEMU.

RNG200 must supply fresh hardware bytes. H2E/own-commit preparation happens
before UP. The password, token and temporary password element are erased;
this command retains the private SAE session only until its bounded exchange
and cleanup finish. This differs from the commit-only probe, which clears the
private session before UP and cannot continue to confirms.

Pi preparation improved from **95 seconds to 3 seconds** in the supplied
captures after replacing serial reduction with fixed P-256 prime folding.
Source loops remain fixed and arithmetic vectors pass. The newest capture
also measures peer key derivation at 2900 ms and verifies the peer confirm.
Wait for progress and the prompt. The long `p256test`,
`h2etest` and `saetest` commands are optional and are not run automatically.

After the peer commit arrives, the synchronous state machine validates it,
derives keys and creates a confirm outside the event callback. It records
`key_math_ms` and refuses a known-late confirm if this calculation takes
4 seconds or more. The event callback only parses/copies bounded data.
There are at most three commit TX attempts and one confirm TX, all with
distinct nonzero packet IDs. A retry preserves the exact commit bytes and
private session; it does not generate new scalars, change the target or
weaken security. It requires SET success, TX ACK 0, no matched RX and a full
five-second wait, beyond the four-second transmit dwell. NACK, missing ACK,
rejected/malformed RX, I/O failure, event flood or a stalled clock stop without
retrying. The commit-only `wifisaeprobe` remains a single transmission.

Each attempt has a 5-second/500-poll bound. Missing commit replies can therefore
take about 15 seconds; a subsequent confirm wait adds at most another five,
plus bounded control I/O/cleanup. The request stage retains its
15-second/1500-poll bound. Confirm retransmission, anti-clogging responses,
optional commit elements and weaker-security fallback remain unimplemented.
Unsupported responses stop with diagnostics.

After the test returns:

```text
wifiinfo
storagetest
pagetest
uptime
```

Share non-password output from preparation onward, including failures. Expected
markers if this first live confirm exchange works are:

```text
SAE confirm test: ok phase=10 join_phase=7
request_seen=yes commit_TX_attempted=yes SET_ack=yes TX_status=0 ...
peer_RX=seen metadata_version=1 sequence=1 status=126 group=19 body_bytes=98 peer_commit_valid=yes reflection=no
confirm_TX_attempted=yes SET_ack=yes TX_status=0 confirm_RX=seen peer_confirm_verified=yes crypto_result=0 key_math_ms=...
Interface DOWN=acknowledged policy/mask restoration=verified transcript_buffers=erased
```

Also capture `commit_attempts`, `active_packet_id`, `observed_auth_RX`,
`early_RX`, `filtered_RX`, `malformed_RX` and the final 12 event diagnostics.
Those rows contain only envelope numbers, not passwords, SAE keys or frame
bodies. Stage 0 means before our first commit TX; 1 means commit exchange;
2 means confirm exchange. Event 187 is the external request, 188 auth RX,
189 TX status and 190 off-channel completion. Other firmware event codes,
including event 0, are kept as diagnostics without treating them as a link.

Filter values are 0=none, 1=interface/BSS, 2=peer, 3=SSID, 4=channel, 5=cookie.
A filtered event never becomes an accepted peer reply. Early RX is counted
but not used to authenticate this new session. Trace rows are printed only
after the synchronous exchange/cleanup, not in time-sensitive callbacks.
On a filtered/unrelated trace row, the codec's "authentication path not ready"
result means that row was ignored; the overall test result is reported above it.

If all three commits are ACKed with `observed_auth_RX=0`, the driver still has
no peer response to verify. For the next attempt, restart the phone hotspot
and cold-boot the Pi to clear pending peer state, keeping the same 2.4 GHz /
WPA3 settings; then share the full diagnostics. An ACK does not establish
that the hotspot's SAE implementation accepted the commit.

`crypto_result=7` is a rejected confirm; it can reflect a credential mismatch
or an interoperability problem, so it is not automatically labelled a wrong
password. No authentication success is declared after a rejected confirm,
NACK, missing event or rejected SET. An unconfirmed DOWN/restoration blocks
further control I/O. Reboot before repeating or changing probe commands.

`crypto_result=not-run` explicitly marks a stop before peer-key processing.
Numeric 0 means the last attempted crypto operation passed; by itself it does
not establish password agreement. Check `peer_confirm_verified` and the
overall test result separately.

## Validation

Native tests exercise a complete two-peer exchange through the actual SAE
session engine and firmware-facing probe state machine. They cover metadata
v1, early and out-of-order confirm/ACK events, different password elements,
bad HMACs, stale commit cookies, NACK, SET failure after events, missing RX,
slow-math refusal, changed peer commits, all 49 normal-path operation failures,
DOWN/restoration, poisoning and full private-session erasure. There is no key
export, success-notification or arbitrary firmware-command hook in that test.

New cases replay ACK-without-RX from the capture, recover on the second/third
commit, verify byte-for-byte/session reuse and five-second spacing, enforce
the three-attempt cap, reject old ACK cookies, stop retry SET/NACK/I/O failures,
and check stalled clocks, early-event floods, bounded trace overwrite and
filtered-envelope trust separation. The fresh RNG is not called by retry code.

The new field reducer is independently compared against BigInt modulo p on
13,647 products including full-width inputs, primes, maxima and bit boundaries.
Existing OpenSSL curve, independent H2E/SAE, rejection and erasure vectors
remain required. Fixed source loops are not a compiler/CPU side-channel audit.

The full native Wi-Fi suite passes with the optimized arithmetic. Pi header,
QEMU virt and handoff builds pass. Cortex-A53/A76 monitor/crypto execution,
the fault test and EL2 USB BOT/FAT firmware-file reads pass. QEMU refuses the
new command without requesting a password. Disk validation, MBR/FAT/kernel
corruption and wrong-console rejection, ZIP integrity and decompressed disk
hash checks pass; read-only emulator tests leave the image hash unchanged.
The newest capture verifies version-1 decoding, peer confirmation and clean
cleanup. Physical retry recovery is not established because its first attempt
succeeded. Association/M1 now has a separate command and hardware test guide.

Primary retry/state reference:
[hostap SME commit construction/reuse](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/wpa_supplicant/sme.c).
The three-attempt, five-second profile here is an OurOS engineering bound.
