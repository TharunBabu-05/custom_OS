# Authenticated Wi-Fi key data and offline message 4

**Hardware result:** this stage now passes on the Pi. Continue with [key installation/live M4](wifi-key-install.md). The details below describe the previous image.


The [latest Pi log](../proofs/pi5-eapol-m3-mic-unwrap-verified-2026-11-10.txt)
passes the full previous probe: SAE peer confirmation, association identity and
security, normalized request IEs, EAPOL M2 transmission and M3 MIC/unwrap.
M2 is 141 bytes; M3 is 187 EAPOL bytes and unwraps to 80 bytes. DOWN, RSN clear,
policy/mask restoration, storage and page tests also pass. This proves the AP
accepted enough of M2 to return a cryptographically valid M3. That probe did
not install keys, send M4 or remain connected.

## This stage

`wifiassoc Jack's` now authenticates the contents of M3 before considering its
key data valid. The selected scan result retains its exact, bounded AP RSN IE.
After the existing replay/ANonce/profile checks and successful MIC/unwrap, the
new parser requires:

- One AP RSN IE matching that selected scan snapshot byte-for-byte. This is the
  **AP advertisement**, which can contain both PSK and SAE and require only PMF
  capability; it differs from our SAE-only/PMF-required station request.
- One CCMP-128 GTK KDE: exact 16-byte key, valid header/reserved bits and key ID.
- One BIP-CMAC-128 IGTK KDE: exact 16-byte key, key ID 4 or 5 and six-byte IPN.
- Bounded TLVs, no duplicate required elements, no unknown elements/selectors,
  and only valid final AES-wrap padding (`dd` followed by zeros, up to seven
  bytes). An arbitrary zero tail is refused.
- A six-byte CCMP receive counter; nonzero upper two RSC bytes are unsupported.

The initial AP profile accepts one CCMP pairwise suite, SAE optionally alongside
ordinary PSK, PMF capability, BIP-CMAC-128 and no PMKID list. Exact RSN comparison
is intentional for this non-FT profile. A changed advertisement requires a new
scan; there is no silent relaxation or WPA2 fallback. The live M2 path still
supports hunting-and-pecking only; H2E M2 needs RSNXE binding.

Parsing happens outside RX callbacks and only after authentication. Private GTK,
IGTK, packet counters and plaintext are erased after validation. Diagnostics
retain success flags, lengths, IDs, padding length and failure offset. They
never print key bytes, decrypted payloads, packet numbers or arbitrary unknown
decrypted tag/selector values. No credential is stored or embedded.

After successful validation, a pure encoder constructs and discards initial
EAPOL M4: 113 Ethernet-frame bytes, descriptor version 0, pairwise/MIC/secure
flags, M3's replay counter, zero nonce/key length/key data and AES-CMAC MIC.
The scoped encoder requires verified M3 key data and the same AP/station MACs
used for M2. `M4_encoded=yes` means bytes were generated, **not transmitted**.
The existing live data-transmit API accepts initial M2 only; it refuses M4.

**This stage still ends DOWN.** No keys are installed and no controlled port is
authorized. M4 transmission and a persistent link remain for the key-installation
stage; IP/DHCP/DNS/ping follow that. A valid M4 encoder alone is not a connection.
The next implementation must handle acknowledged PTK/GTK/IGTK installation,
failure cleanup and M4 retransmission without reinstalling keys before keeping
the link up or authorizing the controlled port.

The format and comparison rules use the primary
[hostap RSN definitions](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.h),
[non-FT RSN comparison](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.c)
and [M3/M4 processing](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/9866086a955d00e237cc8df3722e7dff75c02532/src/rsn_supp/wpa.c).
OurOS uses its own bounded implementation and native public fixtures.

## Physical test

Flash the new complete
[image](../image_files/OurOS-Pi5-header-experimental-sd.img), or extract it from
the [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip), then power-cycle.
Use your existing USB pendrive and CP2102/header console at 115200 8N1 without
flow control. PuTTY Local echo and Local line editing must be **Force off**.

Run each command separately and wait for its prompt:

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

Enter the password only at the hidden prompt. Skip other join/auth/preparation
probes in this boot; reboot before retrying. The long P256/H2E/SAE self-tests
are unnecessary for this run. `keytest` now includes public M4 and key-data KATs.

New expected markers, subject to physical verification:

```text
Association/EAPOL probe: ok phase=8
EAPOL key probe: ok phase=5 M2_SDIO_write=completed (not air ACK)
M3 key data: ok verified=yes AP_RSN_scan_match=yes
GTK_bytes=16 id=... IGTK_bytes=16 id=...
M4_encoded=yes bytes=113 M4_transmitted=no
private_keys_erased=yes
Probe ended DOWN.
```

These lines describe the intended result, not a claimed new hardware pass.
Share `wifiassoc` through `wifiinfo` plus storage/page/uptime results; a full
transcript also works. If validation fails, retain the first error, offset and
cleanup status. No decrypted hex dump is needed.

## Validation

Current artifacts:

| File | Bytes |
| --- | ---: |
| `image_files/OurOS-Pi5-header-experimental-sd.img` | 134,217,728 |
| `image_files/OurOS-Pi5-header-experimental-sd.zip` | 762,837 |
| Packaged `OUROS.IMG` kernel | 276,844 |

Image SHA256: `b476b5df000236bf04b946b8714a1ffef659cb5b8fd7db158708139f5ff99670`.
Previous raw/ZIP images are retained as dated files directly in `image_files`.
The kernel/BSS/stack ends at `0x1f6000`, below the `0x200000` firmware DTB;
the link-time overlap assertion passes.

Native coverage includes 16 independently wrapped/signed M3 fixtures and exact
M4 packets from Node/OpenSSL; single-SAE and PSK+SAE AP RSNs; compact/explicit
management-cipher forms; varied element order and padding; GTK/IGTK and counter
extraction; authenticated malformed, missing and duplicate KDEs; MIC-before-parse;
M4 state/MAC binding; truncations; 10,000 mutations; output bounds and erasure.
The complete SAE/association model tests the new stage with early RX and all
83 per-operation failure points, DOWN/clear/restore and private-state cleanup.
The live transmit model permits exactly one M2 and no M4 in this stage.

The AArch64 `keytest` exercises public M4 and key-data KATs alongside the existing
AES/CMAC/unwrap/PTK/M2 checks. QEMU covers those common components and USB/FAT
regressions; it cannot emulate the Pi's Wi-Fi radio or prove this stage on Jack's.

Completed for this image: the full native Wi-Fi suite; Cortex-A53/128 MiB and
Cortex-A76/512 MiB QEMU monitor, memory, RAMFS, storage and crypto checks; fault
handling; and an EL2 USB BOT/FAT/firmware-file run with all ring wraps and a
host-matched 512-sector hash. Read-only runs leave the image hash unchanged.
The image verifier accepts the delivered filesystem and rejects corrupted MBR,
FAT mirror, kernel payload and mismatched console profile. ZIP CRC testing and
independent decompressed SHA256 comparison pass. Hardware key-data validation
remains untested until a new Pi capture is available.
