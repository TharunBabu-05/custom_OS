# Association IE validation and four-way-handshake foundations

**Current stage: [address checks, ARP and gateway ping](wifi-gateway.md).** The latest Pi log passes corrected M4/key ordering, peer-port setup, DHCP OFFER/ACK and cleanup. The new ARP/ICMP test needs a Pi run. The implementation history below describes an earlier revision.



The [new Pi capture](../proofs/pi5-glom-m1-request-ie-stop-2026-11-10.txt)
passes SAE peer confirmation, firmware authentication-success handoff,
SDPCM aggregate reception and clean DOWN/RSN-clear/policy restoration.
It receives successful ASSOC, LINK and SET_SSID events, reads back the intended
hotspot BSSID/SSID and SAE/AES/required-PMF policy, and observes **EAPOL message 1**.
The receive path now carries real post-authentication traffic.

The remaining stop in that run is the local parser for the firmware-reported
association-request IEs: 122 bytes, phase 3, unsupported format. The log did
not retain those bytes or the exact failing field, so it does not establish
whether the cause is optional RSN encoding, another security field or a missing
element. `association_verified=no` remains correct for the full validation gate.
M1 is unauthenticated and cannot authorize an encrypted data port.

## This revision

The association validator now reports the rejected tag, byte offset and reason:
SSID mismatch/missing, duplicate/missing RSN, RSN security field, RSNXE/method,
WPA1 IE or truncated TLV. RSN details include capabilities, PMKID count and
whether the management cipher was explicit. On rejection, it prints at most
512 bytes of **public association-request IEs** in hex, limited to the declared
length. This does not dump passwords, SAE secrets or EAPOL key material.
SSID and any public PMKID can appear, as in the over-the-air request.

The shared [RSN parser](../lib/wpa_rsn.c) accepts the documented compact forms
with an omitted PMKID field and/or omitted management-cipher field. The default
management cipher is BIP-CMAC-128, as in the primary
[hostap RSN parser](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.c).
This replaces the previous exact-28/44-byte requirement. It still requires
ordinary SAE AKM 8, one CCMP-128 pairwise cipher, CCMP group cipher, both PMF
capability/requirement bits, supported capability flags, correct lengths and a
matching PMKID if present. An explicit management cipher must be BIP-CMAC-128.
The exact SSID and method checks remain; no WPA2 fallback or unverified
association is accepted. Supporting these forms is standards-based; the old
capture does not prove that compact RSN caused its rejection.

This revision also adds original, **offline** cryptographic components for the
upcoming host four-way handshake:

| Component | Implemented and tested | Live use |
| --- | --- | --- |
| AES-128 block encryption/decryption | FIPS197 vectors and OpenSSL comparisons | Not connected to radio key installation |
| AES-CMAC | RFC4493 vectors, block boundaries and long message | Offline MIC calculation |
| AES key unwrap | RFC3394, 2..64 blocks, integrity failure erases output | No received M3 key data processed yet |
| SAE/CCMP PTK derivation | SHA256 IEEE PRF, 384 bits, KCK/KEK/TK split | Public fixtures only |
| EAPOL message-2 encoder | Ethernet/EAPOL framing and AES-CMAC MIC | Builds bytes only; never sends them |

The AES implementation computes substitution through fixed GF(256) arithmetic
rather than indexing a table with key-dependent bytes. Source-level loops are
not an independent compiler/CPU timing audit or production certification.
Expanded keys and temporary working material are erased. The PTK helper binds
the PMK to ordered station/AP MACs and ordered nonces; it is not HKDF. The
pure M2 encoder uses RSN descriptor 2, AKM-defined version 0, key-info `0x0108`,
key length zero, supplied M1 replay counter/SNonce and validated RSN IE. Its MIC
covers the EAPOL header/body with the MIC field initially zero. The caller must
eventually supply a separately verified association and fresh random SNonce;
the encoder does not validate a live M1 or authorize any state transition.
It is an initial, ordinary SAE/CCMP encoder with one RSN IE; rekeying, MLO,
additional RSNXE/KDE elements and extended-key AKMs are outside this helper.

Primary definitions and test-vector sources:
[AES/FIPS197](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197-upd1.pdf),
[CMAC/RFC4493](https://www.rfc-editor.org/info/rfc4493/),
[unwrap/RFC3394](https://datatracker.ietf.org/doc/html/rfc3394),
[hostap PTK derivation](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.c),
[hostap message 2](https://chromium.googlesource.com/external/w1.fi/cgit/hostap/%2B/refs/heads/pending/src/rsn_supp/wpa.c).
The OurOS source is original; these references define its protocol and vectors.

## Run on the Pi

Flash [the full IMG](../image_files/OurOS-Pi5-header-experimental-sd.img), or
extract it from [the matching ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip).
The previous image and ZIP are preserved in `image_files`.
Kernel: **260220 bytes**. Full disk: **134217728 bytes** (128 MiB).
ZIP: **757981 bytes**. Full-disk SHA256:
`d75ea3921aeb3c7f322d503c29bb8ea6c19689b9f0ad021a0fb08a222f67b80b`.

On a **fresh boot**, run separately:

```text
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
wifiassoc Jack's
wifiinfo
storagetest
pagetest
uptime
```

`keytest` is a short, offline check of public AES/CMAC/unwrap/PTK/M2 vectors;
it never requests credentials, accesses the radio or prints key bytes. Expected:

```text
PASS: AES-128, CMAC, key unwrap, SAE PTK and offline EAPOL M2 known-answer tests.
```

Skip other join/SAE/preparation probes in that boot. Keep PuTTY local echo and
local line editing **Force off** and enter the password without quotes only
at the hidden prompt. UART is unencrypted; no credential is saved or embedded.
The long optional arithmetic tests are not prerequisites. Capture the full
`wifiassoc`/`wifiinfo` output, especially **Request IE check**, the RSN fields,
any association-request hex dump, EAPOL M1 and cleanup. Reboot to repeat.

This command still ends DOWN even if its association/M1 gate passes. It does
not retain the live PMK for a handshake or send M2. The next live integration
needs a verified association, fresh SNonce, state-bound M1/M3 replay/MIC checks,
authenticated key-data unwrap/RSN/KDE validation, bounded retransmission,
pairwise/group/management key installation, M2/M4 transmission and controlled
port authorization. IP/ARP/DHCP, ICMP and DNS follow. This is not persistent
Wi-Fi or internet access yet.

## Validation

Native tests compare 24 AES blocks, 12 CMAC messages (including empty, partial,
exact-block and 4096-byte inputs), six unwrap lengths through 512-byte output,
16 SAE PTKs and 16 complete M2 packets with independent Node/OpenSSL fixtures.
Every wrapped ciphertext byte is individually corrupted to check integrity
failure and output erasure. Tests also cover invalid bounds/versions, alias
contracts, nonce/MAC binding, strict RSN defaults/PMKIDs/PMF and malformed data.
The existing complete Wi-Fi suite and request-parser mutation checks remain
required. Published KATs also run through the new optional `keytest` command.

The complete native Wi-Fi suite, independent key fixtures and all existing
SAE/association failure cases pass. The Pi header image builds and passes
filesystem/kernel hashes, corrupted MBR/FAT/kernel and wrong-console rejection,
ZIP integrity and independent decompressed-image hash comparison. The scripts
preserve the previous image/ZIP and never flash a device or commit/push to GitHub.
The Cortex-A53 QEMU monitor and EL2 USB/FAT test also execute the new `keytest`
successfully. The EL2 test verifies packaged firmware-file hashes and 512
read-only sectors against the host, leaving the disk image unchanged.
The Cortex-A76 monitor/key test and A53 fault-handler test pass as well.
These emulator checks do not verify association-request bytes or a live handshake.
