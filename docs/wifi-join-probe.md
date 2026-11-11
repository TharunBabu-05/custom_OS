# External-SAE join-request probe

**Current next step: [`wifiassoc` association/EAPOL observation](wifi-association.md).**
Live SAE peer confirmation now passes on the physical Pi. This page documents
the earlier request-only stage; skip that probe in the new association-test boot.

**Physically verified:** the [new 2026-11-10 capture](../proofs/pi5-external-sae-request-verified-2026-11-10.txt)
passes station initialization, the matching external-SAE request, DOWN and
policy/mask restoration, plus storage/pages. This establishes firmware request
delivery, not authentication. Continue with the [live SAE confirm test](wifi-sae-auth.md)
on a fresh boot, skipping both earlier probes in that boot. The account below
describes the preceding revision and its original image/test procedure.

The [earlier 2026-11-10 Pi capture](../proofs/pi5-station-mode-zero-2026-11-10.txt)
reaches the new probe but reports **INFRA=0**, so the previous read-only
station-mode guard stops before policy changes, UP or join. Storage reads pass;
Jack's is now on channel 11, BSSID 92:b1:03:f4:e3:38, at -33 dBm. The target
comes from the current scan, so neither the channel nor BSSID is hardcoded.

This revision adds normal station initialization. It reads AP and UP state
and requires both to be zero. It accepts INFRA 0 or 1; for 0 it sends the
fixed `SET_INFRA=1` command and requires a successful readback of 1. An
already configured station skips the SET. Other roles, active interfaces,
truncated/rejected reads and failed readbacks stop before join setup. The
station role is checked again after UP before any join request.

Station selection is intentional initialization and remains in place for
the boot; INFRA=0 is not restored after the probe. Security/event snapshots
are taken **after** station initialization and are still restored after
DOWN. This is not yet a Wi-Fi connection. The later capture linked above
now verifies this station setup and request path.
[Reference interface-role setup](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c),
[primary command definitions](https://android.googlesource.com/kernel/common/%2B/bcmdhd-3.10/drivers/net/wireless/bcmdhd/include/devctrl_if/wlioctl_defs.h).

The [preceding Pi capture](../proofs/pi5-auth-normalization-2026-11-09.txt) shows
Jack's on channel 6 at -24 dBm. Both the temporary extended-event mask and the
original security/mask restoration verify. The new auth checkpoints show
`auth=2` immediately after SET auth=3, so the earlier ordering hypothesis did
not explain the readback. The final tuple is:

```text
auth=2 wpa_auth=0x40000 wsec=0x604 mfp=2
```

`wsec` is a bitmask: 0x4 is AES, 0x200 is MFP_CAPABLE and 0x400 is MFP_REQUIRED.
Thus 0x604 is AES plus both PMF flags, not a different cipher. The previous
literal comparison against 4 was too strict. This revision accepts 4 or 0x604
only, alongside the separate required-mfp=2 check. Extra/unknown cipher bits,
partial PMF flags and optional PMF remain rejected.
[Primary flag definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/include/brcmu_wifi.h).

`auth=2` still does not prove SAE. `wifiauthprep` remains a strict diagnostic
and may report that one mismatch. Rather than guessing what the firmware's
getter means, the new `wifijoinprobe <SSID>` observes whether firmware actually
requests external SAE handling for the exact target. This is a bounded probe,
not a connection command. No password or keys are supplied.

## Run on the Pi

The request-only revision had a **207308-byte** raw kernel and a 128 MiB disk.
Its full-disk SHA256 was:
`c53ebeba017aa8c6ccb4a084f632d27e4f6cabddf5faa317d0d5255fc29a43c5`.
Its complete-image ZIP was **733356 bytes** and is preserved under a dated
name. The current `experimental-sd.img`/ZIP have advanced to the
[commit-probe revision](wifi-sae-probe.md), which lists the new sizes/hash
and preferred fresh-boot procedure. The request-only command below remains
available in that image, but cannot precede `wifisaeprobe` in the same boot.
Builds continue preserving older images/ZIPs. No Git commit or push is made
for this phase; the user handles publication.

On a fresh boot, run one command per line and wait for each prompt:

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
wifijoinprobe Jack's
wifiinfo
storagetest
pagetest
uptime
```

The long math self-tests and `wifiauthprep` are optional for this sequence.
Do not put quotes around Jack's. The probe is limited to one 2.4 GHz target
on channels 1..13, selected by the existing clean-scan/cipher/PMF/H2E checks.
It may cause firmware to send discovery/join traffic. Allow 15 seconds plus
control I/O, setup and cleanup time after the probe starts waiting for a request.

Expected success markers, if this firmware supports the path:

```text
External-SAE join probe: ok phase=7
Firmware station mode=1 (requires 1)
Station setup: phase=6 initial=0 AP=0 UP=0 SET_INFRA_attempted=yes selected=verified
request_seen=yes ...
...
external_request_verified=yes
After UP: ... event_mask=verified
Interface DOWN=acknowledged policy/mask restoration=verified
```

With the observed legacy auth getter, raw policy difference bit 0 may still
be shown. The matching external-SAE request, not that raw value, establishes
that firmware reached the requested host-authentication path. It does not
prove the hotspot received a frame or accepted authentication.

Paste the full probe output, `wifiinfo` and final storage/page checks even if
the probe fails. In particular, retain the phase, station mode, after-UP tuple,
event counts, firmware status, DOWN acknowledgement and restoration result.
The request-only path is now physically verified in the latest capture above.

## Scope and cleanup

The driver first initializes an idle non-AP interface to station mode 1 as
described above. It does not replace unknown or active roles. It snapshots policy and the complete event
mask, then applies SAE-only wpa_auth, requested SAE auth, AES and required PMF.
All other controls must read back correctly before radio UP. An acknowledged
auth=3 SET with exactly auth=2 GET may proceed only to this request-observation
path; values 0/1 and unrelated mismatches cannot proceed.

After UP, station mode, the complete security tuple and event mask are read again. A reset
to weaker security or missing events stops before the join command. The join
parameters bind the exact SSID, BSSID and one 20 MHz channel; there is no
wildcard or WPA2 fallback. This uses the reference v0 extended-join layout.
[Primary join structures](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h).

The existing SDPCM channel-1 parser accepts only a successful external-auth
request for that peer/SSID and interface 0. It handles an event arriving before
the join SET acknowledgement as well as an event during subsequent polling.
An event followed by a failed SET acknowledgement is not counted as successful
completion. Wrong-peer/SSID, failed-status, stale and malformed events do not
establish the path. Waiting is bounded by 15 seconds, 1500 polls and an event
budget, including when the clock stalls.

On success, timeout or failure after attempted UP, the probe sends DOWN before
restoring the original policy and full mask. Failed DOWN disables subsequent
control I/O, so policy writes cannot proceed on an interface whose state is
unknown. Failed policy restoration also disables the control path. The prior
event handler is restored. No SAE commit/confirm, `auth_status` success, PMK,
EAPOL, key installation or network data is sent by this code.

The candidate join/SSID cache is not restored. The interface is left DOWN and
the probe is one-shot; reboot before another join attempt, including after a
successful observation. Further invocations display cached status only.

Native tests cover exact 70-byte serialization; the captured auth=2/wsec=0x604
profile; request events before/after ACK; wrong targets/status/lengths;
timeouts, stalled clocks and event floods; security/event resets after UP;
45 injected operation failures; DOWN-before-restore; poisoning and cached
retry refusal. CDC/SDPCM tests also verify the read-only INFRA command and
fixed-size join IOVAR framing. New station-setup transport tests reproduce
INFRA=0, verify the exact five-command AP/UP/INFRA/SET/readback sequence,
already-STA reuse, AP/UP/unknown-role refusal, ignored SETs, 10 per-command
rejection/I/O failures and four truncated replies. No station-setup test
issues UP or join. QEMU explicitly refuses radio probing because
it has no Pi Wi-Fi device.

The Pi build/package/disk checks, image corruption rejection, ZIP integrity,
Cortex-A53/A76 monitor/fault checks and EL2 USB/FAT firmware-file reads pass.
Read-only emulator tests leave the disk hash unchanged. These do not replace
the separate physical SAE commit/reply test.

After physical request delivery is confirmed, the remaining connection work
is protected credential input, live SAE commit/confirm with fresh RNG,
association/EAPOL and key installation with PMF, then ARP/IPv4/DHCP/ICMP/DNS.
Internet access and `ping` are still unavailable.
