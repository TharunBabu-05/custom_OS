# Authentication foundations: random source and crypto primitives

The [latest Pi capture](../proofs/pi5-wifi-scan-wpa3-hotspot-2026-11-09.txt)
confirms passive scanning on real hardware. Five networks are listed with no
malformed results. Jack's appears on channel 6 / 2.4 GHz at -28 dBm, with
RSN+SAE and required PMF. Scan completion, interface DOWN and event-mask
restoration pass. Storage and page tests still pass afterward.

This is a WPA3 advertisement, not a connection. The radio firmware exposes
external SAE, while its supplicant property returns UNSUPPORTED. Before host
authentication can be implemented, OurOS needs fresh random values and tested
key-derivation primitives. This revision adds those foundations. It does not
take a password, initiate authentication or associate with the hotspot.

## Test the new image

Flash the complete header-console image onto the same pendrive:
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
On a fresh boot run these two commands first:

```text
cryptotest
rngtest
```

Candidate image: 134217728 bytes (128 MiB), raw kernel 168564 bytes.
SHA256: `d2da3d384c7eb2ff4f53cb0c1106865f891ceaf0b25a08a44b457823606004c0`.
The adjacent `.sha256` and `.json` identify this image.

Expected successful results:

```text
PASS: HMAC-SHA256 and HKDF-SHA256 published known-answer tests.
RNG200: ok phase=5 ...
PASS: hardware source supplied two distinct 64-byte conditioned samples; sample bytes erased.
```

Then repeat the working radio and regression checks:

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
wifiinfo
storagetest
pagetest
uptime
```

Wait for each prompt. `rngtest` is manually invoked and has bounded waits;
it is never run automatically at boot. It prints configuration/status and
counters, not raw samples, hashes of samples, seeds or keys. Repeating it
prints cached status. Paste the two new command results and the final checks.
If RNG initialization fails, reboot before retrying; random-byte requests
remain unavailable. **The new physical RNG path is UNTESTED.** QEMU has no Pi
RNG200 source and explicitly refuses to substitute simulated entropy.

## Implemented

- Original HMAC-SHA256 and RFC5869 HKDF-SHA256 over the existing SHA256 core.
  HMAC supports vector input and long keys. HKDF supports extract/expand,
  empty salt/info and the 255-block limit. APIs reject invalid inputs and
  enforce bounded lengths; HKDF inputs/output must not overlap.
- Explicit volatile-byte erasure and a comparison that examines every byte.
  HMAC/HKDF temporaries and the SHA message schedule are wiped. This is not
  a claim that all compiler/register state has been independently audited.
- Runtime known-answer tests use public RFC4231/RFC5869 values. Host tests
  also check long/split keys, multiple HKDF vectors, the maximum output size,
  invalid arguments, output boundaries and erasure.
- A narrow Pi RNG200 device-tree gate: BCM2712 root, expected address/size
  cell widths, exact SoC translation, compatible/status/reg properties. The
  pinned DT describes `/soc@107c000000/rng@7d208000`, translated CPU address
  **0x107d208000**, 0x28-byte span. No MMIO is touched unless this gate passes.
- Poll the BCM2711-compatible RNG mode described by that DT. Preserve an
  already-enabled configuration. If disabled, configure the documented
  warm-up threshold, FIFO threshold and sample divider, then verify control
  readback. Do not reset the generator or clear its health-error status.
- Require warm-up progress and discard 16 words before accepting output.
  Check hardware failure/lockout bits, FIFO readiness, repeated consecutive
  words and bounded waits. Reject invalid register reads. Any failure latches
  the source unavailable until reboot, and no fallback uses ticks or MAC data.
- Each 32-byte output block hashes 64 newly read hardware bytes. Requests
  are capped at 256 bytes; raw buffers and SHA state are erased. If any part
  fails, erase the entire caller output and stop further reads. No cached
  pseudo-random stream or entropy-count claim is introduced.
- The manual test draws two independent 64-byte conditioned samples, checks
  they differ, erases both and prints counters only. The driver remains ready
  for a future authentication implementation after success.

These are engineering checks, not an entropy assessment or NIST certification.
Hash conditioning cannot create entropy missing from a faulty source, and
passing a small sample comparison does not establish cryptographic quality.
The implementation and entropy-source assumptions still need review before
real authentication depends on them. No completed SAE protocol is claimed.

## What remains before connecting

1. Verify this physical random-source path and review the cryptographic
   integration; never use a deterministic fallback for authentication.
2. Implement the SAE group operations, password-element derivation, commit/
   confirm processing and its validation/state machine. HMAC/HKDF alone do
   not implement SAE; the exact supported SAE modes must be established.
3. Add external-authentication management-frame transport/events, association,
   EAPOL four-way handshake, key installation and PMF enforcement. Use the
   scanned RSN/cipher capabilities rather than silently changing security.
4. Add protected local credential input with bounded lifetime and erasure.
   A supplied credential is not stored in source, documentation or images.
5. After association: Ethernet delivery, ARP/IPv4/DHCP, then ICMP and DNS.
   There is still no usable `ping`, package installer or internet connection.

The host RNG model uses deterministic values only as test fixtures. Tests
exercise both cold/inherited setup, DT mismatches, timeouts, repeated values,
hardware failure flags and an injected register-I/O failure at every operation
of the successful path, including output erasure after partial generation.

Primary references:
[RNG200 register/mode reference](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/char/hw_random/iproc-rng200.c),
[HMAC test vectors, RFC4231](https://www.rfc-editor.org/rfc/rfc4231),
[HKDF and test vectors, RFC5869](https://www.rfc-editor.org/rfc/rfc5869).
