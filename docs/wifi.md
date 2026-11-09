# Wi-Fi bring-up status

**Passive scanning now passes on the Pi. Association and IP networking are not implemented.**
Core discovery, 800 KiB RAM sizing, [complete firmware upload/readback](wifi-firmware.md)
and [firmware startup](wifi-startup.md) now pass on the real Pi.
[Firmware control queries](wifi-control.md) also pass: ioctl ABI 2, firmware
7.45.265, FWID 01-b677b91b. [Capability and CLM/country preparation](wifi-prepare.md)
also pass on the Pi, as does [passive scanning](wifi-scan.md). Jack's is
observed on channel 6 / 2.4 GHz with RSN+SAE and required PMF. This revision
adds [authentication foundations](wifi-auth-foundation.md): HMAC/HKDF helpers
and a manually tested hardware random source. The [latest capture](../proofs/pi5-wifi-auth-foundation-verified-2026-11-09.txt)
now passes the physical RNG and crypto checks. The [target preflight](wifi-target.md)
also passes on the Pi: CCMP-128, ordinary SAE, required PMF and H2E advertisement.
[P-256 group arithmetic](wifi-p256.md) and [H2E derivation](wifi-h2e.md) now also
pass on the Pi, as does the [in-memory SAE exchange](wifi-sae.md). The next
[external-auth transport stage](wifi-auth-transport.md) adds a frame/event codec
and a reversible firmware SAE/PMF/event-mask check before live authentication.
The [new UART capture](../proofs/pi5-auth-mask-header-2026-11-09.txt) shows a
24-byte mask with `e/v` in the returned version/command positions. Capacity
was not the blocker: our version-echo check rejected this response before SET.
This revision adds compatibility for exactly that captured header, checks its
low 128 bits against legacy `event_msgs`, and requires two identical complete
snapshots before policy changes. Physical confirmation is pending.
Repeat the short radio sequence in [the transport guide](wifi-auth-transport.md);
the long crypto self-tests are optional for this check.
It does not provide a complete FullMAC network
driver, authentication or IP networking. Neither a device-tree entry nor an
SDIO response establishes a working Wi-Fi network interface.

The latest [physical capture](../proofs/pi5-sae-verified-2026-11-09.txt)
confirms D0 board preparation, a configured 400 kHz clock, SDIO selection and
CMD52 CIS identity: vendor 0x02d0, device 0x4345, RCA 1, three functions,
CCCR 3 and SDIO revision 4. Function-1 enable, ALP clock and the backplane read
now pass on the Pi: clock CSR `0x61`, ChipCommon word `0x15264345`, chip `0x4345`,
revision `6`. Five cores, eight banks totalling 819200 bytes and verified bank
selector restoration are now observed. Firmware startup also passes: HT CSR
0xd0, shared pointer 0x201cc0, flags 1, no trap/assert indication. No Wi-Fi
association or IP connection is established by these results. F2 control
replies now also pass; the firmware reports version 7.45.265. The next test
is `wifiauthprep Jack's`, after the proven radio sequence and IN / revision 0.
Measured capabilities include sae_ext and PMF (mfp value 1); sup_wpa reports
UNSUPPORTED, so host authentication work remains. That reply is not an SDIO fault.

That optional extended probe is now removed. Common CIS reads use CMD52, with
the parser stopping at the CIS terminator. The new next step enables function 1
through CCCR IO_ENABLE, waits for IO_READY, requests and verifies the ALP clock,
programs the 32 KiB ChipCommon window and reads its ID with a four-byte function-1
CMD53 request. This sequence is now physically observed. The previous optional
function-0 extended probe failed with R5=0x1100; it remains removed.

The user's requested connection target is **WPA3-Personal**. Connection to an
external router/hotspot remains unfinished. No SSID/password is requested yet.

The [2026-11-09 capture](../proofs/pi5-cold-wifi-and-dma-preflight-2026-11-09.txt)
shows the missing wireless board initialization: CONTROL0=0x00800000,
CONTROL1=0, PRESENT=0x000f0000, CAPS=0x55eec832 and CQE=0. Both wifiprobe and
the previous wifiinit correctly refused that unclocked/unpowered host. No SDIO
command response or Wi-Fi association was obtained in that earlier capture.
The later capture above verifies D0 cold preparation and SDIO identity; the
function-1 identification is now confirmed in the latest capture.

## When can I use a hotspot, ping or install software?

Hardware identification is complete for this Pi; it is not yet a network
interface. USB BOT sector reads, FAT32 `/boot`, file reads after transfer/event
ring wrap and the page self-test are now observed on the real Pi. The immediate
radio work is host authentication foundations after verified passive scanning. The latest
capture verifies all 609309 firmware bytes and 1748 packed NVRAM bytes while
CR4 stays halted during upload, then `wifistart` releases CR4 and confirms
HT/shared memory. Storage and page tests still pass. `wificontrol` now enables
F2, checks protocol/credits and queries the actual firmware version. That
packet/control path now passes physically, as do wificaps and wifiprepare IN.
The latest scan finds four networks including Jack's at -46 dBm with SAE and
required PMF. Crypto, RNG and target preflight tests pass on the Pi. Follow the
[external-auth procedure](wifi-auth-transport.md) to test firmware policy/events.
No connection is attempted yet.

The [first upload capture](../proofs/pi5-firmware-first-transfer-failure-2026-11-09.txt)
passes file hashes and D11/CPU checks but fails on the first code transfer at
0x198000, with no code bytes verified. The current correction adds verified
64-byte F1 setup, explicit host block counting, <=64-byte RAM wire requests
and retained command/IRQ/R5/FIFO diagnostics. The latest upload now passes;
see [the earlier transfer correction](wifi-firmware.md).

The acceptance sequence is:

1. Real pendrive reads, core/RAM discovery and full bounded upload/readback
   with CR4 halted and D11 reset are observed.
2. Firmware, board NVRAM and CLM assets are pinned and packaged. Upload,
   startup and read-only control replies now pass physically.
3. Capability discovery, CLM/country IN and passive scan/events now pass.
   The random source and cryptographic helper tests now also pass on the Pi.
   Check exact target cipher/AKM/PMF advertisements before host SAE.
4. Implement the requested WPA3-Personal SAE/PMF path and protected credential
   input; prove association and key installation against the hotspot.
5. Implement ARP/IPv4, DHCP and routing, then ICMP. First ping the local gateway,
   then a reachable external numeric address. Add DNS for `ping www.google.com`.

ICMP replies can be filtered by a network, so a ping timeout alone does not
establish that Wi-Fi failed. Report association, address, gateway, DNS and packet
counters separately when those services exist. There is no reliable completion
date yet; each hardware/protocol boundary needs its own tests.

Downloading a file additionally needs a supported application protocol such as
HTTP (and TLS for HTTPS). Installing/running programs further needs writable
persistent storage, an executable format/loader, processes and a defined ABI.
OurOS currently has none of those application services and cannot use Linux
`apt` packages. A package manager is a later layer above them.

## Available commands

`wifi` is now an alias of `wifiinfo`. Both read DT/cached driver state and report:

- Current transport/firmware/networking implementation status.
- The actual `wifi0` node path and compatible string from the firmware DTB.
- Firmware-supplied local MAC address, if that property is present.
- The remaining dependencies for a usable connection.
- WPA3-Personal as the requested security target, with connection unavailable.

## Current stage: cryptotest and rngtest

Follow [authentication foundation instructions](wifi-auth-foundation.md) using the complete
rebuilt header image: cryptotest, rngtest, storageinit, wififiles, wifiinit, wificores, wifiload,
wifistart, wificontrol, wificaps, wifiprepare IN, wifiscan, wifiinfo, then storage/page/uptime checks.
The upload command keeps CR4 halted; wifistart explicitly releases it;
wificontrol enables F2 and requests two read-only firmware replies.
wificaps reads features/MAC/PMF/supplicant; wifiprepare IN verifies/uploads CLM
and sets/reads the country. wifiscan requests passive discovery and reports
advertised network/security data; it does not connect. WPA3 and IP remain subsequent work.

## Command: wificores

Rebuild/flash the new header-console image. On a fresh boot run individually:

```text
storageinit
cat /boot/CONFIG.TXT
wifiinit
wificores
wifiinfo
wificores
storagetest
storageinfo
pagetest
uptime
```

Use `storageinit` before `usbtest`/`usbenum`, as before. `wificores` requires
successful `wifiinit` and the observed BCM4345 revision 6 on an AI backplane.
It reads ChipCommon's EROM pointer, bounds parsing to 1024 words/16 cores and
reports core IDs, revisions, register bases and wrappers. ChipCommon, SDIO,
D11 and a unique ARM CR4 core must be present before the RAM query.

The [latest capture](../proofs/pi5-erom-size-field-rejection-2026-11-09.txt)
reaches EROM `0x1810a000`, but the previous parser stops at word 5,
`0x01000000`. That is a raw 16 MiB size payload following address `0x1c000135`,
not a tagged address descriptor. The old tag check was incorrect and is now
removed. The parser consumes the size (and optional upper size word) and skips
that extra address region before continuing. The core-count/table bounds and
RAM-query guards remain. The captured failure happened at cores=0, before any
CR4 bank writes; it left SDIO selected and USB/RAMFS/page tests operational.
Reflash the corrected image before repeating the commands above. Native tests
replay the exact captured six-word prefix with a clearly synthetic continuation;
the following capture now confirms the full core map and 800 KiB RAM size.

The CR4 capability reports its TCM bank count. The query saves BANKIDX, selects
each bank, reads BANKINFO and totals its documented 8 KiB or 1 KiB units, then
restores BANKIDX and reads it back. This is **the Wi-Fi chip's RAM**, separate
from the Pi's main RAM reported by `meminfo`. The `0x198000` RAM base is selected
from the identified chip map; the bank sizes are read from registers, not a
hardcoded capacity. The total is capped at 4 MiB. No chip RAM contents are
written, no firmware is uploaded and neither CPU nor D11 reset is changed.

Successful output includes `core_map=valid RAM=measured`, the core list, bank
sizes, total KiB and `CR4 bank selector restored and verified.` A second
`wificores` prints cached output without further device access. If the core is
held in reset, it reports `RAM=unavailable` and the raw reset state; releasing
that reset is deliberately left for a controlled firmware phase. Unknown or
malformed maps, missing cores and failed transport prevent RAM sizing. Capture
all output, including raw EROM samples on a parse failure. Unconfirmed selector
restoration requires reboot before further Wi-Fi work; transport errors disable
the SDIO attachment. `wifiinfo` includes the cached discovery result.

Native tests exercise actual CMD53 word framing/PIO/window changes plus
R5/CRC/timeouts; a callback model tests both master/slave wrappers, 64-bit/size
payload consumption, missing/endless tables, duplicates, core-count bounds,
RAM units/overflow, reset refusal and selector restoration after failures,
including 10,000 mutated ROM tables. These fixtures do not measure Pi cores or
prove firmware execution. QEMU provides command routing only, with no radio.

## Windows test error dialog

`build/wifi-tests/test-sdio.exe` is the native Windows test for this project's
SDIO/backplane code. It exercises a mock device; it is not the AArch64 kernel
running on the Pi. The reported `0x40000015` is Windows
[STATUS_FATAL_APP_EXIT](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55).
CRT assertion/abort handling can produce that dialog; the screenshot alone
does not identify the particular assertion or prove it is the current binary.
Earlier local test iterations did have assertion failures. The rebuilt current
suite passes. No Windows driver or system-library replacement is indicated.

The Wi-Fi native tests now report failed checks to stderr with file, line and
expression, then exit with status 1. The script still rejects nonzero results.
A dedicated child-process probe verifies this failure path (including NDEBUG)
returns within three seconds, so assertion failures do not wait for a dialog.
The change is limited to host tests; the Pi kernel is unaffected. The probe's
intentional `FAIL` line is saved in
`build/wifi-tests/host-assert-probe.stderr.log`; the script explicitly checks
that expected failure separately from the tests that must pass.

## Command: wifiinit

This is SDIO transport bring-up, **not an external-network connection**. On the
Pi it checks the dedicated Wi-Fi host's state. A cold host now enters narrow
board preparation, guarded by the Pi 5 DT board/wireless/GPIO/clock description,
the exact live BCM2712 stepping register, capabilities and SDHCI version. Unknown
steppings, clocks, layouts or active/inhibited/CQE hosts are refused. A fully
powered/clocked legacy host continues through the existing handoff path.

Cold preparation resets only the Wi-Fi SDHCI host, selects its SD pin route and
non-removable presence override, configures SoC GPIO28 (WL_ON) and GPIO30..35
(SD2), and applies clock/command/data pulls. It holds WL_ON low for 10 ms, enables
3.3V host power, raises WL_ON, waits the board's 150 ms startup delay, and enables
a 400 kHz SDIO clock from the matching 200 MHz DT/capability source. There are
bounded reset/clock waits; failure turns the Wi-Fi clock, host power and WL_ON
off. Unrelated GPIO bits, Bluetooth GPIO29, RP1/UART, PCIe and the SD-card host
are preserved. No firmware mailbox clock API or parent-clock reprogramming is used.

C0/C1 steppings 0x27120020/21 and D0 0x27120030 use their respective mux/pull
register tables, detected from the single register at 0x1001504004. This narrow
driver handles the WLAN pins without relying on the template's C0 pinctrl
compatible string. It does not fix the separate missing D0 overlay packaging
issue or validate other drivers' DT GPIO/interrupt/DMA descriptions. It never
scans adjacent stepping-register addresses. These new board writes are manual,
only via wifiinit. D0 preparation is now observed; C0/C1 still has native tests
only, and the new function-1 work needs physical validation.

On an already powered, clocked legacy host it performs CMD0 reset, matches the
host to the card's default 1-bit width, negotiates a 3.3V OCR with CMD5, obtains
an RCA with CMD3, selects the SDIO card with CMD7, then reads CCCR and the common
CIS with CMD52. CIS parsing is bounded and extracts the manufacturer/device ID;
this identifies the SDIO device, not the backplane chip/revision.
CMD52 common-CIS reads identify the SDIO device without extended function-0
access. The observed Broadcom 0x02d0:0x4345 device then enters a narrow function-1
identification path: IO_ENABLE bit 1 only, IO_READY polling, ALP request/force
with a 65 microsecond settling wait, and three window-address bytes for
ChipCommon 0x18000000. A four-byte CMD53 read at function-1 address 0x8000 uses
the backplane 32-bit access flag. The raw chip ID word, low chip ID and revision
are reported. Unknown SDIO vendor/device IDs are refused before enabling a
function. These are transport/clock/window register writes; no firmware upload,
core reset, RF association, network packets or storage commands are implemented.
In the handoff path power, voltage and clock source/rate are
preserved; cold preparation deliberately establishes 3.3V and 400 kHz first.

Controller waits, OCR retries and CIS traversal are bounded. Failures disable
further reads through that attachment. Once setup changes have been attempted,
reboot before repeating wifiinit. Buffers are local PIO data; SDIO DMA is unused.
Function-1/backplane identification is physically observed. Core discovery and
RAM sizing are now native-tested candidates; firmware download, reset/release
and the FullMAC protocol remain pending.

Flash the new header image to the same pendrive and capture these commands,
using storageinit first if checking the boot drive too:

```text
wifiinfo
wifiprobe
wifiinit
wificores
wifiinfo
```

Include raw CONTROL0/CONTROL1/PRESENT/CAPS/CQE, phase, response and interrupt
values, the board preparation phase/result, stepping and SD clock frequency.
Board phase 6 with SD_clock_hz=400000 means preparation completed; SDIO phase 7
and manufacturer/device values mean device selection and CIS discovery also
completed. A reset/clock timeout or CMD5 failure is not a WPA3 authentication
failure. The first acceptance criterion is a real SDIO response and device ID.

## WPA3-Personal connection requirements

The chip name alone does not establish firmware security capabilities. After
chip/revision identification, pinned board firmware/NVRAM/CLM loading and a
working FullMAC control/event path, query the firmware's actual capabilities.
Broadcom distinguishes `sae`, `sae_ext`/`extsae` and internal supplicant features;
Linux handles both SAE offload and external authentication paths. External SAE
requires host authentication processing, not merely a firmware password setting.

The connection implementation must include SAE (and H2E where required), a
cryptographically secure random source, protected management frames, key exchange
and installation, connection/disconnection events and verified peer security.
Use reviewed cryptographic components for that work. WPA3-only or transition
networks must not be silently connected using an unintended weaker mode. The
requested initial target remains WPA3-Personal; no downgrade is implemented.

Association also needs a configured regulatory country before RF activity. An
associated link still requires an Ethernet data path, ARP/IPv4, DHCP, routing and
DNS for external-network access. Credential input must avoid monitor echo and
logs and support clearing secrets from RAM. These services are not present yet.

The pinned Pi DT describes `/axi/mmc@1100000/wifi@1`, with the family binding
`brcm,bcm4329-fmac`. That string is a compatibility binding, not a measured chip
ID. The board is expected to use a BCM43455-class FullMAC chip, but runtime
identification must precede selecting firmware. Do not substitute firmware for
an arbitrary Broadcom model based solely on that binding string.

`wifiprobe` is a separate manual hardware test. It checks the Wi-Fi SDIO host
at `0x1001100000` and, **only if it is clocked and idle**, issues CMD5 with
argument zero to query R4. Successful output includes the raw response,
advertised I/O function count and ready flag. An unclocked host is reported as
not ready without changing power/clock/pinmux. This command does not negotiate
OCR, select a card, upload firmware, read chip ID, scan, join or transmit IP data.
It may legitimately fail because firmware did not initialize the Wi-Fi device.

The SD-card slot uses a different host, `0x1000fff000`. Reading the boot card
does not implement Wi-Fi's SDIO transport. On QEMU virt, `wifiprobe` explains
that no Pi Wi-Fi device is emulated.

## Remaining implementation sequence

1. D0 cold power/pin/400 kHz setup is observed; C0/C1 has native tests only.
2. Current: guarded SDIO initialization, CCCR/CIS identity and CMD52/CMD53 reads;
   new: guarded function-1 enable/ALP/window writes and one backplane ID read.
   Block-mode transfers and a general backplane interface remain pending.
3. Runtime chip/revision identification is observed (0x4345 revision 6).
   Five cores and 800 KiB CR4 RAM sizing are now physically observed.
4. Licensed, pinned board firmware/NVRAM/CLM and notices are now packaged.
5. Current: verify bounded firmware/NVRAM upload/readback with CR4 halted;
   then implement startup, SDPCM/FullMAC control/event/data and recovery.
6. Scan and WPA3/SAE/PMF association, secure credentials and regulatory country.
7. Network interface queues, ARP/IP/ICMP/UDP/TCP as needed, DHCP, routing and DNS.

Credentials are not requested or stored in this phase. There is no fake
`wifi connect` command and no claim of internet access. Firmware files are
now pinned, downloaded and included after chip/board identity and license
review. Their provenance is in boot/wifi/assets.json and WIFILIC.TXT. The
`wifiinfo` MAC, if present, is board data from firmware,
not a measurement of live association or packets.

Native tests check actual DT inventory, malformed DTB mutations and CMD5 query.
New `scripts/test-wifi.ps1` tests the exact CMD0/5/3/7 initialization, CMD52 CCCR/
CIS, manufacturer identity, CMD53 byte-mode reads including 1/64/512-byte cases,
bounds, timeout/CRC/R5 errors and power/clock/CQE rejection. These are mock-host
tests, not evidence of live radio operation. Physical wifiprobe/wifiinit remain
observed through D0 setup, CIS selection and function-1 ChipCommon identification.
The older function-0 probe failure is documented above. Board tests cover both SoC
layouts, masked writes that preserve unrelated GPIO, startup delays, route and
presence settings, 400 kHz calculation, reset/clock timeouts with shutdown,
unknown DT/stepping rejection and one-shot behavior. QEMU has no onboard radio.
The latest tests also reproduce the captured function-0 R5 rejection and verify
that common reads avoid CMD53 and retain a valid attachment. A mock Broadcom
device exercises function-1 enable, ALP request/force, window selection, ID read
and readiness timeouts. Mock chip IDs are fixtures, not measured Pi results.

References:

- [Pi wireless alias and board bindings](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi)
- [Broadcom SDIO driver/protocol reference](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c)
- [AI EROM and CR4 RAM-bank definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c)
- [ChipCommon EROM pointer](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/include/chipcommon.h)
- [Core identifiers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/linux/bcma/bcma.h)
- [SDIO backplane window and word access](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c)
- [SDHCI host registers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci.h)
- [SD Association specifications](https://www.sdcard.org/downloads/pls/)
- [SDIO command and response definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/linux/mmc/sdio.h)
- [Firmware capability discovery](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/feature.c)
- [FullMAC SAE and external authentication paths](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
- [C0/D0 mux and pull maps](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/pinctrl/bcm/pinctrl-bcm2712.c)
- [SoC GPIO bank/direction registers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/gpio/gpio-brcmstb.c)
- [BCM2712 SD host routing](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci-brcmstb.c)
- [Stepping register described by Raspberry Pi engineering](https://forums.raspberrypi.com/viewtopic.php?t=375346&start=50#p2247856)
