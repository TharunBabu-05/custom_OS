# Wi-Fi EAPOL message 2 / message 3 probe

**Current stage: [authenticated M3 retry handling](wifi-m3-retry.md).** The latest Pi log acknowledges all key SETs/deletions; a third M3 blocks M4. The implementation history below describes an earlier revision.


The [latest physical Pi log](../proofs/pi5-request-ies-fixed-header-2026-11-10.txt)
passes SAE peer confirmation, firmware handoff, aggregate reception, successful
association events, BSSID/SSID and SAE/CCMP/required-PMF readbacks, and EAPOL M1
observation. DOWN, RSN clear, policy restoration, storage and page checks pass.
It stops at our request-IE length interpretation, not at password verification.

## Corrected stop

The dump contains **118 valid IE bytes**, including the correct SSID and a
valid 28-byte RSN. Firmware reports `assoc_info.req_len=122`, which includes the
four-byte fixed association-request header; `assoc_req_ies` excludes that header.
The old caller parsed four bytes of reply padding as duplicate SSID elements.
This ABI convention is documented in the primary
[Broadcom driver implementation](https://android.googlesource.com/kernel/tegra/%2B/android-tegra-moto-2.6.39-jb-mr0/drivers/net/wireless/bcmdhd/wl_cfg80211.c).

The Pi adapter now explicitly subtracts four at the query boundary. It rejects
underflow, reassociation/unknown flags and short replies. The pure IE parser
still rejects duplicate SSIDs and arbitrary trailing zero bytes. No security
check was relaxed. Diagnostics show raw header words, fixed-header bytes and
the normalized IE length. The native suite replays the exact captured bytes.

## New stage

`wifiassoc Jack's` now performs the following bounded test:

1. Complete fresh SAE and verify the peer confirm before firmware handoff.
2. Generate a fresh SNonce from RNG200; keep the PMK in private working state.
3. Require bound association events, identity, policy and normalized request IEs.
4. Bind M1 to the selected AP and station, derive the SAE SHA256 PTK and construct
   initial EAPOL M2 with the actual validated RSN and an AES-CMAC MIC.
5. Send M2 through BCDC interface 0 / SDPCM data channel 2. Recheck the association
   after credit pumping, since callbacks may report a disconnect.
6. Wait for M3, check its profile, higher replay counter and matching ANonce,
   then verify AES-CMAC **before** RFC3394 unwrap. Discard the decrypted bytes.
7. Bring the interface DOWN, clear the owned RSN override, restore policy/events
   and erase private PMK/PTK/nonces/M3 working state on success or failure.

The initial live M2 probe supports **SAE hunting-and-pecking / CCMP-128 / required
PMF**, as advertised by Jack's in this capture. H2E derivation/authentication
remains available in its earlier tests, but a live H2E M2 is refused until RSNXE
binding is implemented. There is no silent downgrade or second-method retry.

RX callbacks only validate/copy bounded bytes. PTK/MIC/unwrap work and transmission
run outside callbacks. M3 can arrive before the SDIO write returns; it cannot
be verified or counted as success until that write completes. Credit and M3
waits are bounded; there is no M2 retry in this revision. A transport failure
disables further transport access, and unconfirmed restoration remains explicit.

**This is still a probe, not a persistent connection.** It sends no M4, validates
no decrypted GTK/IGTK KDEs or AP RSN from M3, installs no keys and never authorizes
the controlled port. It provides no DHCP, IP, DNS or ping. Even a successful
M3 MIC/unwrap test ends DOWN. The new radio path needs a Pi capture.

Wire definitions follow the primary
[hostap handshake implementation](https://chromium.googlesource.com/external/w1.fi/cgit/hostap/%2B/refs/heads/pending/src/rsn_supp/wpa.c),
[hostap key derivation](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.c)
and [RFC3394](https://datatracker.ietf.org/doc/html/rfc3394).

## Pi procedure

Extract and flash the `.img` from
[`image_files/OurOS-Pi5-header-experimental-sd.zip`](../image_files/OurOS-Pi5-header-experimental-sd.zip)
to the pendrive, then power-cycle the Pi. The raw image with the same stem is
also saved directly in `image_files`. Keep previous dated images for rollback.
Use the existing CP2102/header wiring and 115200 8N1 with no flow control.
In PuTTY, set **Local echo** and **Local line editing** to **Force off**.

Run each command separately, waiting for the prompt:

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

Enter the password only at the hidden local prompt. No credential is packaged,
printed or saved. `keytest` is optional if it already passed on this image;
the long `saetest`, `h2etest` and `p256test` are unnecessary for this run.
Skip the other join/auth/preparation probes during this boot; reboot to retry.

For the same firmware/request profile, look for:

```text
Association request fixed_header_bytes=4 normalized_IE_bytes=118
association_verified=yes
EAPOL key probe: ok phase=4 M2_SDIO_write=completed (not air ACK)
M3_bytes=... MIC=verified unwrap_integrity=verified
private_keys_erased=yes
Probe ended DOWN.
```

These are expected markers, **not a claimed hardware result**. Other valid
request lengths can occur. A completed M2 write alone does not prove the AP
received it. Capture `wifiassoc` through `wifiinfo`, plus storage/page/uptime
results; the full boot transcript is also useful. Retain the first failure and
cleanup diagnostics. Do not include passwords or change hotspot security to
work around a failed check.

## Validation and remaining work

Delivered image for this phase:

| Artifact | Size |
| --- | ---: |
| `image_files/OurOS-Pi5-header-experimental-sd.img` | 134,217,728 bytes |
| `image_files/OurOS-Pi5-header-experimental-sd.zip` | 759,554 bytes |
| Packaged `OUROS.IMG` kernel | 264,364 bytes |

Disk SHA256: `e9ba8ec8ae902d9ee1decb609be0943fe6af261b88e317edd52f1c3badc13ce9`.
The previous raw image and ZIP remain as dated files directly in `image_files`.
The linker places the kernel/BSS/stack end at `0x1f3000`, below the firmware
DTB at `0x200000`; the link-time overlap assertion passes.

The native suite covers independent OpenSSL M1/M2/M3 fixtures, exact M2 bytes,
MIC-before-unwrap, corrupted ciphertext, replay/nonce/profile failures, truncated
frames, early RX versus failed writes, credit cancellation, random-source
failure and per-operation failure injection across the complete SAE/association
scope. Public deterministic fixtures are test-only; the kernel uses RNG200.
The existing `keytest` checks public AES/CMAC/unwrap/PTK/M2 known answers in the
AArch64 build. QEMU regression and disk/ZIP verification accompany the image;
QEMU cannot exercise the Pi radio or prove a hotspot connection.

Completed checks for this image: the full native Wi-Fi suite; the association
suite including all 83 per-operation failure points and refusal before M2 for
bad request IEs, deauthentication and unsupported H2E; QEMU Cortex-A53/128 MiB
and Cortex-A76/512 MiB monitor/storage/crypto tests; fault handling; and an EL2
USB BOT/FAT/firmware-file run with ring wraps and a host-matched 512-sector hash.
Read-only tests left the image unchanged. Image verification accepts the image
and rejects corrupted MBR, FAT mirror, kernel payload and mismatched console
profile. ZIP verification checks CRC and independently compares decompressed
SHA256. No new Pi M2/M3 success is claimed by these checks.

After physical M3 verification, implement authenticated KDE/RSN validation,
pairwise/group/PMF key installation and M4 with retransmission handling before
authorizing the port. Then add Ethernet/ARP/IPv4, DHCP, routing, ICMP and DNS.
`ping www.google.com` requires those later layers.
