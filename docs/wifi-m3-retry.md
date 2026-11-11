# Authenticated M3 retries without key reinstallation

**This phase now passes on the Pi:** [physical capture](../proofs/pi5-m3-retry-m4-verified-2026-11-11.txt).
It authenticates three retries, completes three M4 writes and passes identity
and cleanup without reinstalling keys. The current phase is [DHCP](wifi-dhcp.md).
The description below records the preceding implementation and test procedure.

The [latest physical Pi transcript](../proofs/pi5-key-install-m3-retry-stop-2026-11-11.txt)
passes SAE, association, M2 and authenticated M3 key data. **All three firmware
key SETs acknowledge success** (`issued_mask=7`, `ack_mask=7`). Every attempted
key deletion also acknowledges success (`clear_mask=7`); DOWN/RSN-clear/policy
restoration, storage and page checks pass.

M4 was **not attempted**. The stop is `replay/mismatched retry` after a third
EAPOL frame with M3 key-info `0x13c8`, during the second identity query. The
previous receive logic required any later M3 to be byte-identical to the first.
A retransmitted M3 with a higher replay counter necessarily has a different MIC.
This is a likely explanation of the stop, not a proven classification of that
third frame: the old log did not retain its replay comparison or authenticate it.
The new revision supplies explicit retry counters and replay-filter diagnostics.
There is no evidence here of a bad password or failed key SET.

## New behavior

The existing `wifiassoc Jack's` bounded test now handles M3 retries:

1. Receive callbacks check the fixed EAPOL profile, AP/station identity and
   ANonce, then copy at most one higher-replay candidate. No cryptography or
   I/O occurs inside those callbacks. Queuing is not authentication.
2. Outside callbacks, verify that candidate's AES-CMAC MIC, then unwrap its key
   data. Compare a private SHA256 digest of the entire plaintext, and all eight
   RSC bytes, with the first authenticated M3. This includes the GTK, IGTK, IPN,
   RSN and padding. Different keys/counters/data are refused in this narrow
   initial-handshake test; rekey is a separate protocol stage.
3. Preserve the original PTK, GTK, IGTK and installed packet counters. The
   installation journal is one-shot: retry responses cannot reinstall keys.
4. Encode M4 using the latest **authenticated** M3 replay counter and the same
   KCK. If another M3 arrives during transmit-credit pumping, invalidate the
   stale M4, defer its write and authenticate the candidate before regenerating
   the response. A transport error still stops the test.
5. During the existing 20-poll observation window, respond to authenticated M3
   retries with M4 only. Bring the interface DOWN, explicitly delete attempted
   keys, clear the RSN override, restore policy/events and erase private state
   afterward, including failures.

An exact byte-for-byte duplicate of the currently validated M3 may also request
another M4. A lower replay counter, changed bytes at the same counter, changed
ANonce, bad MIC, invalid wrap or changed plaintext/RSC is rejected. A conflicting
second candidate while the single slot is occupied is refused. Limits are
three higher-counter candidates, eight exact duplicates, four completed M4
writes and eight attempts in one transmit-service call. This is bounded retry
handling for the current test, not a persistent supplicant or group rekey engine.

Avoiding reinstallation of PTK/GTK/IGTK on retries follows the primary
[hostap implementation](https://chromium.googlesource.com/external/w1.fi/cgit/hostap/%2B/refs/heads/pending/src/rsn_supp/wpa.c).
OurOS implements its own narrow state machine and checks. A private digest and
RSC snapshot are erased with the session; neither is printed or saved.

**The test still ends DOWN.** Key SET acknowledgements are not key readback or
proof of encrypted traffic. M4 SDIO completion is not an air ACK or proof of AP
acceptance. There is no persistent authorized port, DHCP, IP, DNS or ping yet.
The live profile remains SAE hunting-and-pecking/CCMP-128/required PMF, with no
WPA2 fallback. H2E M2/RSNXE binding and group rekey remain separate work.

## Pi procedure

Flash the complete [image](../image_files/OurOS-Pi5-header-experimental-sd.img),
or extract it from the [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip),
to your USB pendrive. Power-cycle the Pi and use the existing CP2102/header
console at 115200 8N1 without flow control. In PuTTY, force Local echo and
Local line editing off. Run commands separately, waiting for each prompt:

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

Enter the password only at the hidden local prompt. No credential is embedded,
printed or saved. Skip the other join/auth/preparation probes during this boot;
reboot to retry. Long P256/H2E/SAE self-tests are unnecessary for this run.
`keytest` now also verifies a public authenticated M3-retry/M4 known-answer case.

Look for these intended markers, pending physical verification:

```text
Association/EAPOL probe: ok phase=9
M3 retries: queued=... authenticated=... duplicates=... pending=no
M3 retry replay filter=none
M4 writes=... credit_deferrals=... key_reinstall_attempts=0
M4_SDIO_write=completed (not air ACK)
Firmware key installation: ok ... ack_mask=...7
Post-M4 identity=verified
Firmware key cleanup=acknowledged clear_mask=...7
private_keys_erased=yes
Probe ended DOWN.
```

No retry is required if the AP accepts the first M4, so zero retry counters can
be valid. `queued` counts untrusted candidates; only `authenticated` establishes
that a higher-counter M3 passed MIC/unwrap/unchanged-data checks. The original
M3 success flags remain historical if a later candidate fails; use the current
`EAPOL key probe` error and retry counters to assess the stop. The replay filter
distinguishes a counter not above M1, a counter below the latest M3, changed bytes
at the same counter and a conflicting pending candidate. No raw EAPOL, key,
packet-counter or decrypted-payload dump is needed.

Share `wifiassoc` through `wifiinfo` and the storage/page/uptime results; a full
transcript also works. The new hardware retry/M4 behavior remains untested until
that capture is available. Prior dated images and ZIPs are retained for rollback.

## Verification

Current artifacts are saved directly in `image_files`:

| Artifact | Bytes |
| --- | ---: |
| `OurOS-Pi5-header-experimental-sd.img` | 134,217,728 |
| `OurOS-Pi5-header-experimental-sd.zip` | 766,493 |
| Packaged `OUROS.IMG` kernel | 289,220 |

Full-image SHA256:
`43e0aac040d4be98078ea6b8ce9e635c359fb044210665833d25d67e447e5ed3`.
The kernel/BSS/stack ends at `0x1f9000`, below the firmware DTB at `0x200000`;
the link-time overlap assertion passes. Previous complete images and ZIPs
remain as dated files in the same folder.

Tests use 16 independent OpenSSL signed/wrapped higher-replay M3 fixtures and
exact M4 responses. They cover pending-candidate gating, authentication before
M4, unchanged retained group keys, stale/same-counter changes, bad MIC, corrupt
wrap, changed GTK/IPN/RSC, retry/duplicate limits, pre-verification queuing,
private-state wiping and existing parser mutation tests.

The complete SAE/association model injects retries during key SETs, identity
queries, M4 credit waits, the write completion and subsequent polling. It
checks that only three key SETs occur and that retries send M4 without
reinstallation. Both normal and retry paths inject every operation failure
(142 normal, 143 credit-deferral path), with cleanup/poisoning checks. A real
SDPCM transport test verifies that an RX-triggered cancellation prevents the
stale M4 FIFO write while keeping transport available for the refreshed response.
The AArch64 `keytest` includes a public retry KAT; QEMU cannot emulate the Pi
radio or prove that this image stays connected to the hotspot.

Completed validation for this image: full native Wi-Fi suite; targeted key-data
and M4 credit-cancellation checks after the final test additions; QEMU
Cortex-A53/128 MiB and Cortex-A76/512 MiB monitor, memory, RAMFS, storage and
crypto tests (including the new retry KAT); fault handling; and an EL2 USB
BOT/FAT/firmware-file run with ring wraps and a host-matched 512-sector hash.
The image hash stays unchanged after read-only tests. Corruption checks reject
a bad MBR, FAT mirror, kernel payload and console profile. ZIP CRC and independent
decompressed SHA256 checks pass. Physical retry/M4 success remains untested.
