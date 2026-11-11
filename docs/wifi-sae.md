# In-memory SAE commit/confirm exchange

The [new Pi capture](../proofs/pi5-sae-verified-2026-11-09.txt) now passes
`saetest` and the subsequent hardware checks. The tested image is saved as
`image_files/OurOS-Pi5-header-sae-verified.img`. Continue with the
[external-authentication transport/policy check](wifi-auth-transport.md).

The [latest hardware capture](../proofs/pi5-h2e-verified-2026-11-09.txt) passes
H2E, P-256, crypto/RNG, scanning and WPA3 target checks. Storage and page tests
also pass. Jack's is on channel 6 / 2.4 GHz at -41 dBm, with SAE, required PMF
and H2E advertised. The tested image is saved as
`image_files/OurOS-Pi5-header-h2e-verified.img`.

The first `wifiscan\` in that capture contains an extra backslash, so the
monitor rejects it. The subsequent `wifiscan` succeeds. Enter one command per
line without a trailing backslash.

This next revision adds a narrow SAE crypto-exchange engine and `saetest`.
The test runs two synthetic peers entirely in RAM. It checks serialized
commits/confirms, key derivation, rejection of a changed confirm, and controlled
key export. It does not use your hotspot password or contact another device.

## Pi test

For the tested SAE revision, flash `image_files/OurOS-Pi5-header-sae-verified.img` onto
the same pendrive. Size: **134217728 bytes / 128 MiB**. Raw kernel: 190004 bytes.
SHA256: `ce1d563e7a4bcdb25cf2d308dec416fe3cbc06781f73a005ad68eafd7dbc9b79`.

On a fresh boot run, waiting for the prompt after each command:

```text
saetest
h2etest
p256test
cryptotest
rngtest
```

Allow several seconds for the arithmetic. The new expected result is:

```text
PASS: SAE commit/confirm, key derivation, bad-confirm rejection and gated key export.
Synthetic peers only; no hotspot credentials, radio exchange or network connection.
```

Then run the proven hardware checks:

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

Share the new self-test output and final checks. Native tests, Cortex-A53/A76
QEMU monitor tests and EL2-to-EL1 USB/FAT tests pass. Image validation and
deliberate corruption rejection also pass. Execution of this new stage on the
physical Pi now passes in the linked capture. An in-memory test does not establish a real
WPA3 session or internet access.

## Implemented profile

Only group 19, H2E and ordinary SAE AKM 8 are supported by this engine.
Authentication bodies are exactly 98 bytes for commit and 34 bytes for confirm.
The group and confirm counter use little endian; scalars and point coordinates
use fixed-width big endian. The outer 802.11 authentication header/status is
not yet implemented here.

- The caller provides a fresh-randomness callback. The engine samples random
  and mask independently, rejects invalid/repeated scalar pairs, and rejects
  invalid modular sums. At most 16 pairs are tried. A callback error stops
  immediately; no timer, MAC or deterministic fallback is supplied.
- Commit generation computes the modular scalar sum and negative masked
  password element. Exact serialized bytes are retained for retransmission.
  Scalars must be greater than one and less than the group order.
- Peer processing checks body length, group, scalar range and canonical
  on-curve coordinates. Reflected commits and a shared intermediate at infinity
  are rejected. Invalid peer data erases the session's secret material and
  latches failure until an explicit reset.
- Shared-point X feeds keyseed derivation. The IEEE SHA256 KDF derives KCK/PMK
  using the protocol label, scalar-sum context and little-endian counter/bit
  length. This KDF is separate from HKDF-Expand. PMKID uses the leading 16
  bytes of the padded modular scalar sum.
- The first local confirm has counter 1; retransmissions return the same
  cached bytes. Peer confirms use the opposite commit order. Fresh peer
  counters 1..65534 are allowed; zero and 65535 are refused in this limited
  profile. A changed proof cannot release a PMK. Repeated accepted proofs
  cannot advance the state again.
- Key export is allowed once, after a local confirm has been generated and
  the peer's confirm verified. Export clears the session. Random/PWE material
  is wiped after shared-key derivation; KCK is wiped after peer verification.
  Derived keys and scalars are never printed by `saetest`.

The engine's local-confirm state means **generated**, not transmitted. No
management frame is sent by these APIs. Inputs and outputs must not overlap
session state; programming errors and wrong-state calls are refused.

## Validation and remaining work

`scripts/test-sae-session.ps1` checks twelve public exchange fixtures against
an independent affine BigInt model, Node HMAC and OpenSSL shared-X results.
Commit bodies, both confirms, PMKs and PMKIDs must match exactly. Additional
tests cover truncated/extended bodies, invalid groups/scalars/points,
reflection, cancellation, changed passwords, altered confirms/counters,
replay, RNG failure/partial output, bounded rejection, modular sum endpoints,
alias/state guards and secret erasure. The suite also runs through
`scripts/test-wifi.ps1`. Synthetic callbacks are confined to the test paths.

This is not a complete on-air SAE state machine or an interoperability result
against the hotspot. Optional IEs, rejected-group salt, anti-clogging tokens,
password-identifier negotiation, SAE-PK and extended-key AKMs are refused or
absent; they are not silently ignored. Timers, session expiry, retry policy,
address/status/sequence validation and radio transport remain outside this
backend. Future callers must bind it to one peer and clear it on timeout.

External-authentication management-frame/event transport, hidden local input
and fresh hardware RNG are now integrated in the experimental
[live SAE confirm test](wifi-sae-auth.md); physical confirm verification is
pending. Association and the EAPOL four-way handshake/key installation with
PMF remain. No independent side-channel audit is claimed for this live path.
ARP/IPv4/DHCP, ICMP and DNS follow a protected link. There is no package manager
or internet connection yet.

Primary protocol references consulted for the original implementation:
[hostap commit, key derivation and confirm handling](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/sae.c),
[IEEE SHA256 KDF encoding](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/crypto/sha256-prf.c).
