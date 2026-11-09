# WPA3 target advertisement checks

Hardware-tested preflight disk image: 134217728 bytes, raw kernel 173620 bytes.
SHA256: `060c8f9d6c409360fbb2a7e7703414b258096067fb3575133f708f35ff9e44c2`.
The archived copy is
`image_files/OurOS-Pi5-header-wpa3-target-verified.img`.
The newest build and next test are described in [P-256 group arithmetic](wifi-p256.md).

The [latest Pi log](../proofs/pi5-wpa3-target-verified-2026-11-09.txt)
passes HMAC/HKDF known-answer tests and the manual hardware RNG test. Both
64-byte conditioned RNG samples are erased without printing their contents.
The radio sequence still passes, finding Jack's on channel 6 / 2.4 GHz at
-34 dBm with SAE and required PMF. Storage and page tests pass afterward.

This revision adds `wifitarget <exact SSID>`. It examines the completed cached
scan and verified firmware capabilities without issuing radio commands or
accepting credentials. The earlier scan retained general SAE/PMF flags but
discarded cipher selectors and RSNXE; authentication needs those details.

After flashing the latest **header-console complete disk image** onto the same
pendrive, run each command separately, waiting for the prompt:

```text
cryptotest
rngtest
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
wifiinfo
storagetest
pagetest
uptime
```

The apostrophe and spaces are literal. Do not surround the SSID with quotes or
append a password. This command consumes the rest of the line as the SSID.

The preflight requires exact ordinary SAE AKM `00:0f:ac:8`, CCMP-128 pairwise
and group cipher, PMF capability, and BIP-CMAC-128 management protection. Where
the optional management cipher is omitted, the parser retains its AES-CMAC
default. An AP may advertise optional PMF, but the future station policy remains
required PMF. FT-SAE-only, PSK-only, unsupported ciphers, incomplete scans and
ambiguous same-SSID BSSIDs are refused. No fallback changes the requested WPA3
policy. It reports BSSID/channel/suites and retained RSNXE octets, including the
SAE hash-to-element advertisement. That bit alone does not establish an agreed
password-element mode or a working SAE implementation.

Native tests cover these policies, selector OUIs, optional/default suites,
PMKID bounds, RSNXE bounds/duplicates, binary SSIDs and truncated advertisements.
Existing scan/SDPCM transport, crypto and RNG tests also pass. The [physical
preflight capture](../proofs/pi5-wpa3-target-verified-2026-11-09.txt) now passes:
SAE AKM 8, CCMP-128, BIP-CMAC-128, required PMF and RSNXE `0x20` / H2E.
A compatible advertisement is
unauthenticated and does not establish AP identity or a connection.

Next work remains SAE group/password-element/commit/confirm processing, external
authentication frame/event transport, association, EAPOL/key installation and
PMF enforcement. DHCP/ARP/IPv4, ICMP and DNS follow a working protected link.
There is no `ping` or package installer yet. Credentials are not embedded in
source, documentation or images.

Format references consulted for the original parser/policy:
[Broadcom RSN selectors and PMF handling](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c),
[RSNX capability length and H2E bit](https://github.com/torvalds/linux/blob/master/include/linux/ieee80211.h).
