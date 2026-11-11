# SAE without an H2E advertisement

**Now physically verified:** the [new capture](../proofs/pi5-sae-hunt-confirm-glom-stop-2026-11-10.txt)
passes H&P peer confirmation in the first attempt, 4-second preparation,
2702 ms key calculation and firmware authentication-success acknowledgement.
It then stops at unsupported SDPCM aggregation, leaving cleanup unconfirmed.
Continue with [the aggregate receive correction](wifi-sdpcm-glom.md).
The rest of this page describes the preceding H&P implementation/test phase.

The [latest Pi log](../proofs/pi5-sae-no-h2e-advertisement-2026-11-10.txt)
finds Jack's at BSSID `76:2d:65:57:03:c9`, channel 6, -43 dBm. It advertises
both PSK and SAE, PMF capability and **no RSNXE**. Earlier captures advertised
SAE with required PMF and H2E. The current settings therefore differ from the
ones used for the successful H2E confirm test. The current BSSID/channel always
come from the scan; they are not embedded in the driver.

The old H2E-only guard refuses this attempt before credential input or radio
UP. It is not a failed password or evidence that the previous RSN fix failed:
that part of the driver did not run. Storage passes. The typed `pagetime`
command is unknown; use `pagetest` for the allocator check.

## What changes

OurOS now chooses its SAE password-element method once from the validated scan:

| Advertisement | Selected method | SAE commit status |
| --- | --- | --- |
| RSNXE advertises H2E | Hash-to-element (existing path) | 126 |
| H2E absent, SAE AKM 8 present | Hunting-and-pecking (new path) | 0 |
| Malformed method information or no ordinary SAE AKM | Refuse | No transmit |

The new method remains **WPA3/SAE**, CCMP-128 and required station PMF. An AP
may advertise both PSK and SAE, but OurOS selects only SAE AKM 8. It does not
try PSK, change the security policy or switch methods following a failed
authentication. A status-126 reply to a status-0 exchange (or the reverse)
stops with diagnostics. If you restore the hotspot's previous WPA3/H2E
settings, the existing H2E path is selected automatically.

The original [sae_hunt.c](../lib/sae_hunt.c) computes the group-19 PWE before UP:

1. Order the two MACs as MAX || MIN to form the HMAC key.
2. For counters 1..40, calculate HMAC-SHA256(password || counter), then the
   256-bit IEEE KDF with the `SAE Hunting and Pecking` label and P-256 prime.
3. Reject noncanonical X candidates; test the curve square root and choose Y
   parity from the seed's low bit. Select the first valid candidate with masks.
4. Evaluate all 40 candidates even if the first succeeds. If none succeeds,
   fail without radio UP. Erase the password and all temporary derivation data.

Unlike H2E, this derivation has no SSID salt or intermediate PT. The new
`p256_from_x` function evaluates a fixed square-root exponent even for invalid
candidates and masks failed output to zero. The loop does not report its
first-success counter. Fixed source loops/masks are not an independent
compiler or CPU side-channel audit. No production hardening certification is
claimed for this experimental kernel.

The existing fresh RNG, commit/confirm, reflection checks, peer confirmation,
PMKID handoff, association-request security checks and cleanup remain required.
For this ordinary group-19, no-rejected-groups profile, the SHA256 KCK/PMK
derivation is the same after PWE selection. Optional password identifiers,
anti-clogging responses, rejected groups and other curves remain unsupported.

Primary protocol references:
[hostap SAE derivation and key processing](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/sae.c),
[IEEE SHA256 PRF framing](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/refs/heads/main/src/crypto/sha256-prf.c).
These define the protocol; the implementation here is original code.

## Run on the Pi

Flash the newly built full [IMG](../image_files/OurOS-Pi5-header-experimental-sd.img)
or extract it from the [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip).
Kernel: **239084 bytes**. Full disk: **134217728 bytes** (128 MiB).
ZIP: **749252 bytes**. Full-disk SHA256:
`7ea199a31f8fd0a72680e7181e4b8a263ab1f2296c46d78f6066cc69551cf0a1`.

Use your pendrive as before. On a **fresh boot**, run individually:

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
wifiassoc Jack's
```

Skip other join/SAE/preparation probes in this boot. The command reports its
chosen SAE method before the hidden password prompt. Set PuTTY local echo and
local line editing **Force off**; enter the password without quotes only at
that prompt. No credential is saved, printed or put in an image. UART is
unencrypted. Wait for the prompt during the fixed 40-candidate preparation.
The long optional `p256test`, `h2etest` and `saetest` are not prerequisites.

Then capture `wifiinfo`, `storagetest`, `pagetest` and `uptime`. Share the full
command output without password input. Check the method line, peer confirm,
RSN setter/query diagnostics, `association_verified`, association-request IEs,
EAPOL M1 and cleanup results. See [the association guide](wifi-association.md)
for all markers. Reboot before another attempt.

## When will it stay connected?

This build removes the method-selection blocker shown in the newest log.
Hunting-and-pecking and the association path still need a successful physical
Pi capture. The earlier H2E peer-confirm success does not prove this new path.
`wifiassoc` remains an observation test and ends DOWN even on success.

A persistent encrypted Wi-Fi connection needs the host four-way handshake,
EAPOL replies, replay/MIC checks and pairwise/group/management key installation,
then authorization of the data port. Those are still unimplemented. IP/ARP,
DHCP and routing follow for IP traffic; ICMP and DNS are needed for
`ping www.google.com`. There is no reliable completion date until these
hardware and protocol checks pass. A known password alone cannot supply the
missing handshake or network-stack implementation.

## Validation

The native checks generate 62 canonical/noncanonical X/parity vectors and 24
PWE vectors with independent BigInt arithmetic and OpenSSL point decoding.
All PWEs match; both MAC orders and binary/max-length password inputs pass.
Each PWE also completes matching simulated SAE confirms and key export.
Invalid arguments, overlapping output and failure erasure are checked.

The firmware model exercises an actual H&P-derived PWE through status-0
commit/confirm, handoff and association/M1. Bad confirms and cross-method
status mismatches stop without another method attempt. Existing H2E and
fault-injection tests remain required. The optional `saetest` now also runs a
fixed public H&P known-answer check, enabling arithmetic verification on
QEMU AArch64. It does not use your hotspot password or access the radio.

The full native Wi-Fi suite and the updated SAE session self-test pass. The
Pi header image builds, its filesystem and kernel hashes validate, damaged
MBR/FAT/kernel and wrong-console copies are rejected, and the ZIP passes
integrity and decompressed-image SHA256 checks. The previous image and ZIP
are retained in `image_files`. No physical device was flashed by these scripts,
and no Git commit or push was performed.
QEMU Cortex-A53/A76 monitor and arithmetic tests, the A53 fault-handler test
and EL2 USB BOT/FAT firmware-file reads also pass. The updated optional SAE
self-test executes the H&P KAT on both emulated CPUs. Read-only tests leave
the image hash unchanged. These checks cannot verify the radio connection.
