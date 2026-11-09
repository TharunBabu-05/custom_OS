# Wi-Fi capabilities, CLM and country preparation

The [latest physical capture](../proofs/pi5-wifi-control-verified-2026-11-09.txt)
passes both version queries: ioctl ABI 2, firmware **7.45.265**, FWID
**01-b677b91b**. F2 is ready, mailbox reports SDPCM version 4, two requests
receive valid replies, and two event frames are drained. Storage and page
tests still pass. This verifies communication with running firmware, not
association, WPA3 support or IP access.

This revision adds `wificaps` and `wifiprepare <CC>`. The confirmed country for
your test is **IN (India)**. It is supplied explicitly in the command; the
kernel does not guess it from a MAC address or silently select another region.
The [new physical capture](../proofs/pi5-wifi-prepare-verified-2026-11-09.txt)
passes both commands: sae_ext, PMF value 1, sup_wpa UNSUPPORTED, CLM status 0,
country/ccode IN revision 0. The next [passive scan candidate](wifi-scan.md)
is UNTESTED on the Pi. Preparation itself does not scan or bring the interface up.

## Test using the USB pendrive

Flash the newly built complete image:
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
Use the same USB port and CP2102 settings, 115200 8N1. On a fresh boot run
individually and wait for each prompt:

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
wifiinfo
storagetest
pagetest
uptime
```

Observed preparation image: 134217728 bytes (128 MiB); raw kernel: 148412 bytes.
SHA256: `bc3f693557eeb3b6e4a302fddd5845942b1da40d33c80b8b315f08f341c42eab`.
It is preserved as `build/rpi5-header/OurOS-Pi5-header-wifi-prepare-verified.img`.
The current candidate adds scanning; use its adjacent `.sha256`/`.json` and
[new instructions](wifi-scan.md).

Wait for all 609309 firmware bytes to be verified by wifiload. Startup must
reach shared_memory=ready and control must reach queries=verified before
capability discovery. Expected new milestone lines are:

```text
Wi-Fi capabilities: ok phase=5 discovery=verified
Wi-Fi regulatory preparation: ok phase=6 CLM/country=verified
```

`wificaps` prints the actual capability string and firmware MAC. It reports
exact `sae`, `sae_ext`/`extsae` tokens and reads `mfp` (PMF) and `sup_wpa`
(firmware supplicant). A successful GET returning **0** means the property is
available with current value zero; it does not mean the feature is absent or
enabled. An explicit firmware UNSUPPORTED response is reported separately.
Other valid firmware rejections leave that property's support unconfirmed
and retain its status code; transport/protocol errors stop discovery.
No SAE token alone proves that OurOS can connect
to WPA3; host authentication, key handling and connection/event code are still
missing. Absence of firmware SAE offload is also not proof that a future host
SAE implementation is impossible.

`wifiprepare IN` rechecks the exact 2676-byte CLM file and its pinned SHA256
before any CLM/country SET. It uploads two verified-source chunks (1400 and
1276 bytes), checks clmload_status=0, reads CLM version text, sets country IN
with unspecified revision (-1), then reads it back. Success requires both the
returned country abbreviation and ccode to be IN, with a resolved revision.
An unexpected mapping or fallback stops this phase for inspection; scanning
is not started. These settings live in firmware RAM and must be reapplied
after reboot.

Paste the complete results, including the cap string, SAE/PMF/supplicant
lines, CLM version, upload counters, country/ccode/revision and final tests.
If anything fails, run wifiinfo before rebooting. Include the phase, firmware
status, raw country reply, last SDPCM header and first SDIO error. A partial
CLM/country operation requires reboot before retry. Repeated commands print
cached state without reloading or changing the country.

## Implementation and bounds

- The original wificontrol startup remains GET_VERSION + GET_VAR("ver").
  Additional IOVAR operations preserve its live SDPCM sequence, request IDs,
  receive state and transmit credits; they do not reset F2 or the radio CPU.
- GET is restricted to cap, cur_etheraddr, mfp, sup_wpa, clmload_status,
  clmver and country. SET is restricted to clmload and country. There is no
  monitor interface for arbitrary firmware requests or credentials.
- The maximum IOVAR buffer is 1536 bytes, TX frame <=2048 bytes; the newer scan
  path accepts RX frames up to 4096 bytes. TX continues
  incrementing addresses across <=512-byte PIO commands; RX stays at the fixed
  frame port. A partial-transfer error stops immediately and disables further
  control exchanges. The F1 upload's existing 64-byte limit remains unchanged.
- The IOVAR request carries name+NUL+data/output space, matching the reference
  wrapper. CDC lengths, GET/SET flags, IDs, payload extent and output-copy
  bounds are checked. A valid firmware rejection is distinguished from a
  transport/protocol failure, so querying an unsupported optional property
  does not destroy an otherwise healthy control channel.
- CLM is buffered and SHA256-checked before upload. Each chunk carries the
  downloader version, BEGIN/END bits, CLM type 2, actual byte length and CRC=0.
  No firmware file is changed. CLM version text is bounded and sanitized.
- Country input is exactly two uppercase letters; placeholders, extra text
  and world-code input are rejected. Firmware rejection is preserved. Actual
  returned bytes are retained for diagnosing an unexpected revision/mapping.
- Capability and preparation commands are bounded and one-shot. No automatic
  recovery/reset can race running firmware. No persistent disk/EEPROM writes.

Native tests cover the pinned real CLM, exact upload bytes and flags, hash and
file failures, firmware rejections, country mismatch/fallback, strings, MAC
validation and exact feature-token matching. Every capability/preparation
operation has an injected failure case. Control tests cover larger GET/SET
frames, multi-part replies, request/sequence wrap and poisoning the channel
after transport failure. PIO tests validate continued TX addresses and stopping
before a rejected second fragment. QEMU tests exercise the commands' refusal
on a machine without the Pi radio and continue USB/FAT checks.

After this physical test passes, the next stage is scan request/event handling
under the confirmed country, followed by WPA3 authentication/association.
ARP/IPv4/DHCP, ICMP and DNS remain after a working association.

Primary references:
[capability tokens and PMF/supplicant probing](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/feature.c),
[CLM download protocol](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/common.c),
[CLM and country structures](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[IOVAR request construction](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.c).
