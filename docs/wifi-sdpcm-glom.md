# SDPCM aggregate reception after successful SAE

**Now hardware verified:** the [new capture](../proofs/pi5-glom-m1-request-ie-stop-2026-11-10.txt)
passes one aggregate/two subframes, link/identity/security, EAPOL M1 and cleanup.
The next stop is association-request IE validation. Continue with
[IE diagnostics and the key-handshake foundation](wifi-key-handshake.md).
The account below documents the earlier aggregate implementation phase.

The [latest Pi capture](../proofs/pi5-sae-hunt-confirm-glom-stop-2026-11-10.txt)
passes hunting-and-pecking SAE on Jack's: a valid group-19 peer commit,
both TX acknowledgements and `peer_confirm_verified=yes`. Preparation takes
4 seconds, peer key calculation 2702 ms, and the first commit attempt succeeds.
Firmware also acknowledges `auth_status` success. This verifies the new SAE
method and the earlier fresh-station RSN setter path on hardware.

Association is **not verified**. After an AUTH event, the receive path rejects
a new framing type. The recorded three little-endian header words are:

```text
ffef0010 0c188353 00006800
```

They decode to length 16 with complement `0xffef`, RX sequence `0x53`, channel
byte `0x83`, next-length hint `0x18`, data offset 12 and TX window `0x68`.
Channel 3 plus bit `0x80` identifies an SDPCM **glom descriptor**. Its four-byte
payload describes two coalesced receive segments. The log did not save those
four payload bytes, so their lengths and contents are unknown.

The old parser rejected every raw channel byte above 2. It disabled the
transport before association events could be processed; subsequent DOWN,
RSN-clear and policy restoration consequently remained **unconfirmed**.
This failure is in SDIO packet framing after successful SAE. It is not evidence
of a rejected password. Storage and page tests still pass afterward. Reboot
before another radio attempt because that run did not confirm cleanup.

## New receive support

The original implementation in [brcm_control.c](../drivers/storage/brcm_control.c)
now understands the bounded descriptor/superframe protocol described by
[the primary brcmfmac SDIO driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c#L1432):

- Decode the low channel nibble separately from the descriptor flag. Reserved
  flag bits and unknown channels remain rejected.
- Accept 1..16 little-endian segment lengths. The first includes the outer
  header; every segment must fit a complete inner header. Reject zero, short,
  oversized, odd-length descriptor lists and sums beyond the 8 KiB profile.
- Read the complete superframe using the descriptor's sum rounded to the
  negotiated 512-byte block size. Each FIFO read is at most 512 bytes. There
  is no speculative 64-byte first read for an already described aggregate.
- Validate the outer header, block-rounded size, offset, TX credit window and
  all inner headers before delivering **any** callback. Inner frames must be
  ordinary event/data channels with valid bounds and consecutive RX sequences.
  Control frames, nested aggregates and descriptors inside aggregates fail.
- Deliver validated event/data payloads in wire order through the existing
  BCDC parsers. Empty inner packets are skipped. An aggregate cannot authorize
  association or a data port by itself; all higher-layer checks still apply.

The driver supports ordinary CDC replies interleaved with aggregates. It counts
descriptors, superframes and subframes under the existing bounded RX budget.
It uses a fixed 8 KiB receive buffer, not DMA or a heap. Descriptor errors,
read failures, malformed subframes or budget exhaustion stop and disable
further transport I/O. No speculative drain, restart or arbitrary firmware
command is attempted after an untrusted framing failure.

`wifiinfo` now prints `SDPCM glom:` counters, phase/error, the last length list,
described/wire size, outer header and last examined inner header. These are
framing diagnostics, not passwords, key bytes or packet-body dumps. Phases:

| Phase | Meaning |
| --- | --- |
| 1 | Validate descriptor and calculate bounded wire size |
| 2 | Read block-rounded superframe |
| 3 | Validate outer header and credit window |
| 4 | Validate all inner headers |
| 5 | Aggregate accepted; callbacks delivered |

## Pi procedure

Flash the newest [full image](../image_files/OurOS-Pi5-header-experimental-sd.img)
or extract it from [the matching ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip).
Kernel: **247372 bytes**. Full disk: **134217728 bytes** (128 MiB).
ZIP: **751387 bytes**. Full-disk SHA256:
`4317ae595980447c9092632044fd551610127563e40c7f58c688427704d747d6`.

Use the pendrive as before. On a **fresh boot**, run separately:

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
wifiinfo
storagetest
pagetest
uptime
```

Skip the other join/SAE/preparation probes in that boot. Keep PuTTY local echo
and local line editing **Force off**; type the password without quotes only
at the hidden prompt. It is not saved or embedded. UART is unencrypted.
Long optional arithmetic tests are not prerequisites. Share the full output
without password input, including the new aggregate diagnostics and cleanup.
Reboot to repeat any started attempt.

Check `peer_confirm_verified=yes`, `firmware_auth_success_SET=acknowledged`,
`association_verified=yes`, `Association request IEs=verified` and EAPOL M1,
plus DOWN, RSN clear and security/event restoration. An accepted aggregate
only establishes framing progress; it cannot substitute for these checks.

## Validation and remaining work

The host model replays the exact captured descriptor **header**, with explicitly
synthetic length/payload fixtures. It checks event/data delivery, a subsequent
CDC ACK during DOWN, 512-byte read boundaries, padding, extended offsets,
sequence wrap, all operation failures and 17 malformed cases. It also checks
1/16-subframe and 8 KiB bounds, empty packets, 10,000 header mutations and
5,000 descriptor/outer/inner-header mutations. Rejected aggregates deliver no
callbacks, including when an earlier inner header is valid. Existing SAE,
association, cleanup and independent crypto tests remain required.

The full native Wi-Fi suite passes, including the existing SAE/H2E/H&P
checks. The expanded aggregate boundary/mutation test also passes against the
final receive implementation. Pi header, virt and handoff builds succeed.
Disk-image validation, MBR/FAT/kernel corruption and wrong-console rejection,
ZIP integrity and independent decompressed-image hash comparison pass.
Complete previous disk images and ZIPs are retained in `image_files`; the
builder does not flash hardware or commit/push to GitHub.
QEMU Cortex-A53/A76 monitor and crypto tests, the A53 fault test and EL2 USB
BOT/FAT firmware-file reads pass. Read-only emulator tests leave the delivered
image hash unchanged. QEMU has no Pi radio; these are kernel/transport-regression
checks, not physical aggregate or association verification.

Hardware aggregate support and association/M1 still need the new Pi capture.
The command still ends DOWN even on success. Four-way-handshake replies,
authenticated key-data processing, pairwise/group/management key installation
and port authorization remain unimplemented. IP/ARP/DHCP, ICMP and DNS follow
those stages. The current image does not provide persistent Wi-Fi or internet.
