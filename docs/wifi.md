# Wi-Fi bring-up status

**Scanning, connecting to an access point and IP networking are not implemented.**
This phase provides actual firmware-DTB inventory plus an opt-in SDIO CMD5
diagnostic. Neither a device-tree entry nor an SDIO response establishes a
working Wi-Fi network interface.

## Available commands

`wifiinfo` reports:

- Current transport/firmware/networking implementation status.
- The actual `wifi0` node path and compatible string from the firmware DTB.
- Firmware-supplied local MAC address, if that property is present.
- The remaining dependencies for a usable connection.

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
2. SDIO initialization, function discovery and CMD52/CMD53 register/block transfers.
3. Runtime chip/revision identification and backplane/core access.
4. Properly licensed, pinned board firmware, board NVRAM/calibration and CLM data.
5. Firmware upload/start, SDPCM/FullMAC control/event/data protocol and recovery.
6. Scan and association/security configuration, country/regulatory settings.
7. Network interface queues, ARP/IP/ICMP/UDP/TCP as needed, DHCP, routing and DNS.

Credentials are not requested or stored in this phase. There is no fake
`wifi connect` command and no claim of internet access. Firmware files are not
downloaded or redistributed until their exact chip/board identity and license
are established. The `wifiinfo` MAC, if present, is board data from firmware,
not a measurement of live association or packets.

Native tests check the actual Pi template alias/property lookup, malformed DTB
mutations, a successful simulated CMD5 response and an unclocked-host rejection.
The physical `wifiprobe` path remains untested pending the user's serial capture.

References:

- [Pi wireless alias and board bindings](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi)
- [Broadcom SDIO driver/protocol reference](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c)
- [SDHCI host registers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci.h)
- [SD Association specifications](https://www.sdcard.org/downloads/pls/)
