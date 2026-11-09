# Wi-Fi bring-up status

**Scanning, connecting to an access point and IP networking are not implemented.**
This revision adds an opt-in SDIO initialization/identity/read transport to the
existing DT inventory and CMD5 query. It does not provide a FullMAC firmware
driver, authentication or IP networking. Neither a device-tree entry nor an
SDIO response establishes a working Wi-Fi network interface.

The user's requested connection target is **WPA3-Personal**. Connection to an
external router/hotspot remains unfinished. No SSID/password is requested yet.

The user's physical USB-boot capture confirms `wifiinfo` reads the firmware
node, family binding and MAC address. It does not show `wifiprobe` or a Wi-Fi
connection. Booting from a pendrive does not change the on-board Wi-Fi SDIO
transport. The active next phase is [USB controller discovery](usb.md) for the
user's actual boot device. USB controller observation now has physical evidence;
the new DMA and wireless SDIO initialization tests need their own captures.

## Available commands

`wifi` is now an alias of `wifiinfo`. Both read DT/cached driver state and report:

- Current transport/firmware/networking implementation status.
- The actual `wifi0` node path and compatible string from the firmware DTB.
- Firmware-supplied local MAC address, if that property is present.
- The remaining dependencies for a usable connection.
- WPA3-Personal as the requested security target, with connection unavailable.

## New command: wifiinit

This is SDIO transport bring-up, **not an external-network connection**. On the
Pi it checks the dedicated Wi-Fi host's existing state. An unclocked, unpowered,
inhibited, CQE, high-speed/UHS, non-3.3V or otherwise unsupported setup is refused
before controller writes. Missing board power/clock/pinmux setup is still a
real blocker; the command does not program GPIO28, regulators or clock sources.
The BCM2712 D0 overlay packaging/pin-description gap must also be resolved before
implementing board GPIO setup; a successful UART does not validate Wi-Fi pins.

On an already powered, clocked legacy host it performs CMD0 reset, matches the
host to the card's default 1-bit width, negotiates a 3.3V OCR with CMD5, obtains
an RCA with CMD3, selects the SDIO card with CMD7, then reads CCCR and the common
CIS with CMD52. CIS parsing is bounded and extracts the manufacturer/device ID;
this identifies the SDIO device, not the backplane chip/revision. A short CMD53
byte-mode function-0 CIS read exercises PIO data transport. No CMD52 register
writes, function enablement, firmware upload, RF association or storage commands
are implemented. Power, bus voltage and clock source/rate are preserved.

Controller waits, OCR retries and CIS traversal are bounded. Failures disable
further reads through that attachment. Once setup changes have been attempted,
reboot before repeating wifiinit. Buffers are local PIO data; SDIO DMA is unused.
Function-1/backplane access and function enablement still need implementation.

Flash the new header image to the same pendrive and capture these commands,
preferably before the separate USB controller takeover test:

```text
wifiinfo
wifiprobe
wifiinit
wifiinfo
```

Include raw CONTROL0/CONTROL1/PRESENT/CAPS/CQE, phase, response and interrupt
values. A clean `device not ready` is a useful result when the wireless host
was never initialized by firmware. It is not a WPA3 authentication failure.

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

1. Correct board-specific power/reset and clock/pin setup for the wireless host.
2. Current: guarded SDIO initialization, CCCR/CIS identity and CMD52/CMD53 reads;
   pending: function enablement, writes, block-mode and backplane access.
3. Runtime chip/revision identification and backplane/core access.
4. Properly licensed, pinned board firmware, board NVRAM/calibration and CLM data.
5. Firmware upload/start, SDPCM/FullMAC control/event/data protocol and recovery.
6. Scan and WPA3/SAE/PMF association, secure credentials and regulatory country.
7. Network interface queues, ARP/IP/ICMP/UDP/TCP as needed, DHCP, routing and DNS.

Credentials are not requested or stored in this phase. There is no fake
`wifi connect` command and no claim of internet access. Firmware files are not
downloaded or redistributed until their exact chip/board identity and license
are established. The `wifiinfo` MAC, if present, is board data from firmware,
not a measurement of live association or packets.

Native tests check actual DT inventory, malformed DTB mutations and CMD5 query.
New `scripts/test-wifi.ps1` tests the exact CMD0/5/3/7 initialization, CMD52 CCCR/
CIS, manufacturer identity, CMD53 byte-mode reads including 1/64/512-byte cases,
bounds, timeout/CRC/R5 errors and power/clock/CQE rejection. These are mock-host
tests, not evidence of live radio operation. Physical wifiprobe/wifiinit remain
untested pending the user's serial capture. QEMU has no onboard radio.

References:

- [Pi wireless alias and board bindings](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi)
- [Broadcom SDIO driver/protocol reference](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c)
- [SDHCI host registers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci.h)
- [SD Association specifications](https://www.sdcard.org/downloads/pls/)
- [SDIO command and response definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/linux/mmc/sdio.h)
- [Firmware capability discovery](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/feature.c)
- [FullMAC SAE and external authentication paths](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
