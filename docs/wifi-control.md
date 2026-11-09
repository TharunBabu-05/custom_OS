# Read-only Wi-Fi firmware control channel

The [startup capture](../proofs/pi5-firmware-startup-verified-2026-11-09.txt)
confirms wifistart reaches `ok phase=7 shared_memory=ready` on the real Pi. Clock CSR
is 0xd0, shared memory 0x201cc0, shared flags 1, console 0x25debc, trap 0 and
assert line 0. Mailbox 0 is expected before function 2 is enabled. USB sector
reads and page tests still pass afterward. This establishes firmware boot
initialization; it does not establish association or an IP connection.

The next command is **`wificontrol`**. It enables SDIO function 2 and exchanges
SDPCM/BCDC control packets with the firmware. It issues exactly two read-only
requests: GET_VERSION (ioctl ABI), then GET_VAR("ver") (firmware version text).
No radio-enable, scan, association, credentials, SET commands or IP packets are
sent. Country/CLM setup and security-capability discovery remain later work.
Native tests and the [physical control capture](../proofs/pi5-wifi-control-verified-2026-11-09.txt)
now pass: phase=8 queries=verified, ioctl ABI 2, firmware 7.45.265,
FWID 01-b677b91b. The next test is [capabilities and CLM/country](wifi-prepare.md),
whose larger transfers and regulatory commands remain UNTESTED on the Pi.

## Hardware test

Flash the new complete header-console image onto the same USB pendrive:
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
Keep the same USB port and CP2102 terminal at 115200 8N1. On a fresh boot run
these commands individually, waiting for each prompt:

```text
storageinit
wififiles
wifiinit
wificores
wifiload
wifistart
wificontrol
wifiinfo
storagetest
pagetest
uptime
```

Observed control image size: 134217728 bytes (128 MiB); raw kernel: 133716 bytes.
SHA256: `f5ba5932dc934e6229e55b8825ebd6c318cc8d2ec927c74650cc10e0c6ebd11c`.
That image is preserved as `build/rpi5-header/OurOS-Pi5-header-wifi-control-verified.img`.
The current candidate at the standard path adds capability/regulatory commands;
use its adjacent `.sha256`/`.json` files and [new instructions](wifi-prepare.md).

`wifiload` must finish with all 609309 code bytes verified. It can take several
minutes. `wifistart` must report shared_memory=ready. Then `wificontrol` should
report:

```text
Wi-Fi control channel: ok phase=8 queries=verified
Firmware ioctl ABI=2
Firmware version: <the actual text returned by your chip>
```

ABI 1 is also accepted for these two simple queries. The version string is
never hardcoded as a simulated success. A successful result requires a valid
reply to each request, with matching command and request ID and no firmware
error status. It still does not prove WPA3 support, a working network interface
or internet access.

Paste the complete results through the final tests. If control startup fails,
capture `wifiinfo` before rebooting. Include phase, F2 block size, IO_ENABLE,
IO_READY, clock CSR, interrupt/mailbox, TX/RX counters, transmit window, request
ID, command, firmware status, last address/word, last RX header and any first
SDIO failure.
Repeated `wificontrol` only prints cached status; reboot before retrying.
No reset, disable or cleanup write is attempted against running firmware on
failure. A ready flag is the last confirmed observation, not proof that a
later failed transport is still usable.

## Driver contract

- Require successful `wifistart` and the identified BCM4345 revision-6 profile.
  Recheck live HT clock and require F1 enabled with F2 initially disabled.
  No register writes occur before these checks.
- Force HT, configure/read back F2's 512-byte FBR block size, advertise host
  SDPCM protocol 4, preserve other function-enable bits while enabling F2, and
  poll IO_READY. Use the reference default watermark 8: the measured SDIO
  device ID is **0x4345**, whereas the reference 43455 tuning case is **0xa9bf**.
- Poll SDIO-core interrupt/mailbox registers with CPU interrupts still masked.
  Acknowledge host mailbox indications, require a version-4 DEVREADY/FWREADY
  indication and reject FWHALT. Do not enable save/restore clock modes yet.
- Program the ChipCommon backplane window for every F2 transfer. Use aligned
  4..512-byte CMD53 byte-mode PIO: incrementing writes, fixed-address reads at
  0x8000, as in the reference bus helpers. No DMA/block mode or automatic retry.
  F1's already-tested 64-byte RAM wire limit remains unchanged.
- Wait for a firmware-advertised transmit credit before sending either
  request. Read each unknown packet's first 64 bytes, validate its SDPCM
  header, then read the rounded remainder in bounded chunks. Read through
  queued frames until the all-zero empty header or an appropriate reply.
  Limit frame size to 2048 bytes, total received frames to 64, stale responses
  to 8 and each wait to 500 iterations with 10 ms counter-based delays.
- Validate length/complement, channel, data offset, credit window, CDC payload
  extent, interface/GET flags, request ID, command, returned length and firmware
  status. Record sequence resynchronization. Drain bounded event/data frames
  without interpreting or delivering them; reject glom/test/unsupported frames.
- Copy only bounded reply data. Require a nonempty NUL-terminated version
  string; reject control/escape bytes and display permitted whitespace as
  spaces. No credential or arbitrary firmware-command monitor interface exists.
- The command is one-shot. Report cached historical startup and control
  milestones separately; never infer a current halted CPU from old discovery.

| Phase | Meaning |
| --- | --- |
| 1 | Startup/live-clock/function-enable checks |
| 2 | HT, F2 block size and host protocol setup |
| 3 | F2 readiness |
| 4 | Firmware mailbox protocol/readiness |
| 5 | Initial transmit credit |
| 6 | GET_VERSION request/reply |
| 7 | GET_VAR("ver") request/reply |
| 8 | Both read-only queries verified |

## Validation and next steps

Native tests cover setup, delayed readiness, packet continuation, exact query
bytes, credit-only frames, event/data draining, stale IDs and sequence changes.
They inject an I/O error at all 37 operations of the successful sequence and
exercise malformed headers, lengths, channels, flags, firmware errors, missing
credits/readiness and invalid version strings (28 negative states). SDIO PIO tests check F2 address
mode, 512-byte zero-count encoding, backplane window, R5 refusal before data
and stopping on a mid-FIFO CRC error. Existing F1 upload/startup tests remain.
QEMU verifies the monitor command refuses a nonexistent Pi radio, plus USB/FAT
file reads; it cannot validate this physical FullMAC exchange.

After real control replies pass: query firmware security capabilities, load
CLM and configure a country, implement scan/events, then WPA3 SAE/PMF and
protected credential input. ARP/IPv4/DHCP, ICMP and DNS follow association.

Primary references for register/protocol definitions:
[SDPCM, mailbox, clocks and setup](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c),
[F2 address modes/window](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c),
[BCDC request/reply format](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcdc.c),
[GET command numbers](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.h),
[measured versus tuned SDIO IDs](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/linux/mmc/sdio_ids.h).
