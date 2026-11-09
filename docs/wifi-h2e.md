# SAE H2E password-element derivation

The [new physical capture](../proofs/pi5-h2e-verified-2026-11-09.txt) now passes
`h2etest` and the subsequent crypto/RNG/radio/storage/page checks. That image
is preserved as `image_files/OurOS-Pi5-header-h2e-verified.img`.
Continue with the [in-memory SAE exchange test](wifi-sae.md).

The [latest Pi log](../proofs/pi5-p256-verified-2026-11-09.txt) passes `p256test`
twice, then crypto/RNG, firmware/country preparation, scanning, WPA3 target
checks, storage and pages. Jack's is on channel 6 / 2.4 GHz at -40 dBm with
ordinary SAE, CCMP-128, required PMF and H2E advertised. Its tested image is
preserved as `image_files/OurOS-Pi5-header-p256-verified.img`.

This revision adds the group-19 H2E derivation backend and the manual command
`h2etest`. The command uses synthetic public inputs. It neither accepts your
hotspot password nor starts authentication. Real credentials are not embedded
in the source, images or documentation.

## Test this image

For this tested H2E revision, flash `image_files/OurOS-Pi5-header-h2e-verified.img`
onto the same pendrive.
Complete disk size: **134217728 bytes / 128 MiB**. Raw kernel: 185908 bytes.
SHA256: `dcacc03c45a59c7e57e7b875600115b18ed7b173e0826cc890ab638ed61ee3e3`.

Run these on a fresh boot, waiting for each prompt:

```text
h2etest
p256test
cryptotest
rngtest
```

The math tests can take several seconds. Expected new result:

```text
PASS: SAE H2E group-19 mapping, password token and MAC-bound password element.
Public fixtures only; no hotspot credentials or radio packets. Not connected.
```

Then repeat the working hardware sequence:

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
storagetest
pagetest
uptime
```

The linked physical capture now passes this H2E test and the final checks.
The tests also pass natively and in AArch64 QEMU on
Cortex-A53/A76, including EL2-to-EL1 boot with USB/FAT reads. Disk-image
validation and deliberate corruption rejection pass. A math-test pass does
not prove a WPA3 connection.

## Implemented

- A separate SHA256 HKDF-Expand API, so H2E expands an already extracted seed
  without extracting it a second time. The existing full HKDF API continues
  to pass its vectors; both APIs now enforce their no-overlap input contract.
- P-256 SSWU with Z=-10, 48-byte reduction modulo the field prime, explicit
  zero-denominator handling and sign selection from the reduced input's parity.
  Both candidate roots are computed and selected with masks at source level.
- SAE password-token (PT) derivation: extract with the SSID, expand the two
  distinct protocol labels, map each field value and add the resulting points.
- Password-element (PWE) derivation: hash the sorted MAC pair with a zero HMAC
  key, reduce to `1..n-1`, then multiply PT by that scalar. That public MAC hash
  is not an authentication RNG or an ephemeral secret.
- Bounded byte-oriented backend inputs: SSID 1..32, synthetic/password input
  1..128, optional identifier 0..64 bytes. Zero, multicast and identical MAC
  pairs are refused. These are local API limits, not a complete provisioning UI.
  Point encodings are 64-byte big-endian `X || Y`; SAE frame encoding is separate.
- No input/output overlap for the H2E API; failures clear its output. Seed,
  expanded values and temporary points/scalars are explicitly wiped. No derived
  value is printed by the kernel command.

Host tests cover 32 SSWU cases, including an RFC9380 intermediate, all three
exceptional mapping inputs, prime boundaries and the largest 384-bit input.
Six binding-hash boundary vectors check reduction. Sixteen synthetic PT/PWE
sets compare against a separate affine BigInt model; OpenSSL also validates
curve encodings and the resulting PWE X coordinates. Tests cover optional
identifiers, binary/max-length inputs, MAC-order symmetry, invalid arguments,
overlap refusal, output bounds and erasure. HKDF-Expand uses an RFC5869 vector.
`scripts/test-sae-h2e.ps1` runs these checks and is included in `test-wifi.ps1`.

The runtime self-test uses one published RFC9380 map intermediate and a fixed
synthetic PT/PWE fixture. Its public strings are test data and must never be
used as live credentials. RFC9380 verifies the underlying map; the SAE-specific
labels and PT/PWE construction follow the protocol reference below. This is
not an implementation of the entire RFC9380 hash-to-curve suite.

## Still needed before a connection

The new derivation is isolated from the radio and live credentials. It needs
cryptographic integration and side-channel review before live authentication;
fixed loops/masked selections alone do not establish compiler/CPU constant-time
behavior. No interoperable handshake has been demonstrated with another SAE
implementation or the hotspot.

Next are SAE scalar/random handling, commit/confirm validation and key
derivation, anti-clogging/retry handling, external-auth frame/event transport,
protected credential entry, association and EAPOL/key installation with PMF.
ARP/IPv4/DHCP, ICMP and DNS follow a protected link. There is no internet access
or package installation yet.

Primary references:
[hostap SAE derivation and H2E mapping](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/sae.c),
[RFC9380 SSWU and test intermediates](https://www.rfc-editor.org/rfc/rfc9380.txt),
[RFC5869 HKDF](https://www.rfc-editor.org/rfc/rfc5869.txt).
