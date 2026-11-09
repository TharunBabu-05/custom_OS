# Passive Wi-Fi scan

The [latest Pi capture](../proofs/pi5-wifi-prepare-verified-2026-11-09.txt)
passes capability discovery and CLM/country setup. Firmware reports `sae_ext`,
PMF property value 1, and an explicit UNSUPPORTED response for `sup_wpa`.
CLM upload is acknowledged for all 2676 bytes, clmload_status is zero, and
country/ccode IN with revision 0 is read back. USB/page tests still pass.
The unsupported supplicant property is a capability result, not an SDIO error.

This revision adds **`wifiscan`**. It requests a passive scan and lists SSIDs,
BSSIDs, band, primary channel, RSSI and advertised security/PMF information.
Native tests and the [physical scan capture](../proofs/pi5-wifi-scan-wpa3-hotspot-2026-11-09.txt)
now pass. Five networks include Jack's (channel 6 / 2.4 GHz, -28 dBm,
RSN+SAE, PMF required); malformed=0, interface DOWN and mask restoration pass.
The next stage is [authentication foundations](wifi-auth-foundation.md). Scanning does
not associate, send credentials or implement an IP connection.

## Test on the same pendrive

Flash the new complete image:
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
Keep the same USB port and CP2102 at 115200 8N1. Put your router/hotspot in
range, then on a fresh boot run each command separately:

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

Observed scan image: 134217728 bytes (128 MiB); raw kernel: 160372 bytes.
SHA256: `99a7a861b6958ee8befa2a392f3bce6fd4fc33912b6b6ff57d4c0aed9cd3ef41`.
Preserved as `build/rpi5-header/OurOS-Pi5-header-wifi-scan-verified.img`.
The current standard image adds crypto/RNG tests; consult its adjacent
`.sha256`/`.json` and the authentication-foundation instructions.

Wait for every prompt. Upload, startup, control and preparation must all pass.
`wifiscan` can take about 30 seconds plus bounded control I/O. It uses the
firmware's country-filtered default channel list with passive listening,
wildcard BSSID/SSID and no directed probe request. No channel list or country
override bypasses the verified IN configuration.

Expected successful milestone:

```text
Wi-Fi passive scan: ok phase=8 scan=complete
```

The output should show discovered network rows and acknowledge interface DOWN
and restored event mask. A completed scan can contain zero results. If the
malformed/unsupported-result count is nonzero, zero rows do not prove that no
networks were present; include those diagnostics in your capture.

Paste everything from wifiscan through the final tests. Useful diagnostics are
phase, escan version, sync ID, firmware/event status, reason, result/event/
malformed counts, last BSS version, control headers and first SDIO failure.
On failure capture wifiinfo, then reboot before retrying. Failure after UP can
leave the interface or scan active; the monitor reports that uncertainty and
does not issue speculative cleanup writes. A repeated wifiscan only displays
its cached result; reboot to run another scan in this revision.

SSID bytes are untrusted. Hidden SSIDs appear as `<hidden>`; non-ASCII,
control, quote and backslash bytes are escaped as `\xHH` so they cannot send
terminal control sequences. Network rows show advertisements, not trusted
identity or a successful security negotiation. `RSN+PSK+SAE` indicates the AP
advertises both suites; it does not mean OurOS has connected with either.

## What was added

1. Require completed CLM/country preparation and ioctl ABI 2. Read country
   again before any radio action and require the saved code/revision.
2. Query scan_ver. Select the reference v2 request when that query succeeds;
   use v1 only after explicit firmware UNSUPPORTED. Preserve and extend the
   current legacy event mask for ESCAN_RESULT (event 69), then verify readback.
3. Request interface UP and a single passive escan. No association, SSID
   selection, key setting or authentication request exists. The firmware
   chooses its permitted channels, with a 150 ms passive dwell request.
4. Process events even while waiting for the scan command acknowledgement.
   Strip the bounded BCDC prefix, check Ethernet/Broadcom event identity,
   interface, event version/type, payload extent and scan sync ID. Retain
   terminal status separately from the command acknowledgement.
5. Parse BSS version 109 only, with bounded lengths and IE offsets (including
   the 126-byte wire prefix). Decode primary channel/band and signed RSSI.
   Parse RSN AKM lists and PMF flags, with all IE/count extents checked.
   Keep up to 32 BSSIDs/bands, merge duplicate observations and report overflow.
6. Bound the RX frame to 4096 bytes, scan event budget to 1024 frames and wait
   to 3000 polls/30 seconds plus individual bounded I/O. Unsupported or malformed
   scan records are counted and skipped. Invalid SDPCM framing/transport errors
   stop the channel rather than continuing after uncertain packet boundaries.
7. On successful terminal completion, request interface DOWN, restore the
   original event mask and verify readback. Overall scan success requires both
   result completion and cleanup. Failure is one-shot and requires reboot.

| Phase | Meaning |
| --- | --- |
| 1 | Live country and scan-version checks |
| 2 | Event-mask subscription and readback |
| 3 | Interface UP |
| 4 | Passive escan request/acknowledgement |
| 5 | Result/completion events |
| 6 | Interface DOWN |
| 7 | Event-mask restoration/readback |
| 8 | Scan and cleanup complete |

Native tests cover v1/v2 request bytes, synchronous events, per-operation
failures, timeouts, aborts, cleanup failures, country mismatch and cached
refusal. Parser tests cover RSN/PMF, lengths, hidden/duplicate/overflow results,
RSSI/channel decoding and 5000 mutated packets. An integrated SDPCM/CDC test
delivers a result larger than 2 KiB before the SET acknowledgement and terminal
completion afterward. QEMU tests the command's explicit refusal without a
Pi radio while keeping the existing USB/FAT tests.

## WPA3 implications

The measured firmware offers external SAE and PMF but does not expose the
firmware supplicant property. This is not a completed WPA3 path or proof that
the hardware cannot support it. OurOS still needs host SAE, secure randomness,
cryptographic/key handling, association and the EAPOL four-way handshake with
PMF enforcement. No password is needed for this scan test. DHCP/IPv4/ARP, ICMP
and DNS remain after a working association.

Primary references:
[scan requests and completion handling](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c),
[request and BSS layouts](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[event envelope and status codes](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fweh.h),
[BCDC event prefix](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcdc.c),
[D11AC channel decoding](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmutil/d11.c).
