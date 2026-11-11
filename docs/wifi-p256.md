# SAE group arithmetic: P-256

The [new Pi capture](../proofs/pi5-p256-verified-2026-11-09.txt) now passes
`p256test` twice and the subsequent radio/storage/page checks. This tested image
is backed up as `image_files/OurOS-Pi5-header-p256-verified.img`.
Continue with the [H2E derivation test](wifi-h2e.md).

The [latest hardware capture](../proofs/pi5-wpa3-target-verified-2026-11-09.txt)
passes `wifitarget Jack's`: CCMP-128 group/pairwise ciphers, ordinary SAE AKM 8,
BIP-CMAC-128, required PMF, and RSNXE `0x20` advertising hash-to-element (H2E).
Six networks are listed without malformed results; storage and page tests pass.
The tested preflight image is preserved as
`image_files/OurOS-Pi5-header-wpa3-target-verified.img`.

This next revision implements the P-256 group operations needed by the future
SAE implementation and adds a manual `p256test` command. It does not implement
password-element derivation, SAE messages or association. H2E advertisement does
not prove the hotspot supports only one password-element mode; OurOS has not
negotiated a mode with it.

## Pi test

For the tested P-256 revision, flash `image_files/OurOS-Pi5-header-p256-verified.img` onto
the same pendrive. It is **134217728 bytes / 128 MiB**; the raw kernel is 181812
bytes. SHA256:
`b484c8f14a139d6175ed4938c1f293cb01e162e92001b33a4e088ccabbf07e01`.

On a fresh boot run:

```text
p256test
cryptotest
rngtest
```

Wait for the prompt after each command. `p256test` performs fixed public-vector
calculations; allow several seconds. Expected result:

```text
PASS: P-256 RFC5903 public/shared points, scalar boundaries and group identities.
Math self-test only; no credentials, radio packets or Wi-Fi connection.
```

Then repeat the proven hardware checks:

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

The linked capture verifies this test and the final checks on the physical Pi.
Native tests and AArch64 QEMU (Cortex-A53/A76, plus EL2-to-EL1 with USB/FAT)
pass. Image validation and deliberate corruption rejection also pass.
The test does not need a password or the hardware random source. None of its
fixed published values may be used as a live authentication key or random seed.

## Implementation and checks

- Original freestanding C arithmetic using eight 32-bit limbs, 64-bit products,
  512-bit products with fixed P-256 prime folding, modular addition/subtraction
  and inversion. The earlier serial reduction loop is replaced in the current
  [live-confirm revision](wifi-sae-auth.md), checked on 13,647 BigInt products.
- P-256 point validation, Jacobian doubling/addition, scalar multiplication and
  point negation. Input scalars must be in `1..n-1`; coordinates must be canonical
  and on-curve. Point-at-infinity results are rejected. P-256 has cofactor one.
- Fixed 256-round scalar multiplication with masked point selection and
  exceptional-case handling. No scalar-indexed table or early exit on leading
  zero bits. This describes source-level control flow, not an independently
  audited constant-time implementation on every compiler/CPU.
- Fixed-width big-endian APIs: scalar is 32 bytes; point is 64 bytes `X || Y`,
  without an SEC1 prefix. Future SAE packet encoding must perform the required
  wire conversion separately. Failed operations erase the output. Inputs can
  overlap output; temporary point/scalar/product buffers are explicitly wiped.
- Runtime tests check RFC5903 public/shared point answers, scalars 0/1/2,
  doubling, negation and inverse-point cancellation. Host tests add `n-1`,
  out-of-range scalars, noncanonical/off-curve points, null arguments, aliasing,
  output bounds and failure erasure. Forty independent OpenSSL vector sets
  compare scalar multiplication, addition and negation against Node's backend.

Run the host tests with `scripts/test-p256.ps1`; they also run as part of
`scripts/test-wifi.ps1`. Node is used only to generate public host-test fixtures;
the kernel has no Node/OpenSSL runtime dependency. Existing radio/SDIO/parser,
crypto and random-source tests continue to run separately.

## Remaining connection work

The backend is now used by the experimental [live SAE confirm test](wifi-sae-auth.md).
H2E, hidden input, fresh RNG, commit/confirm processing and external-auth frame
transport are implemented; physical confirm verification is pending. Remaining
work includes anti-clogging/retry handling, association and EAPOL/key installation
with PMF enforcement. Generic curve test vectors do not establish SAE interoperability
or password side-channel resistance. ARP/IPv4/DHCP, ICMP and DNS follow a protected
link. There is still no internet connection or package installer.

Primary references for parameters, public fixtures and formulas:
[RFC5903 sections 3.1 and 8.1](https://www.rfc-editor.org/rfc/rfc5903.txt),
[EFD Jacobian formulas for a=-3](https://www.hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-3.html).
The SAE H2E mapping is a separate protocol task; implementing a generic hash-to-
curve suite alone would not implement that protocol.
