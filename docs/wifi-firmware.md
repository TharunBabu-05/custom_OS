# Wi-Fi firmware upload and readback

The [physical capture](../proofs/pi5-wifi-cores-800k-ram-2026-11-09.txt) now
confirms the corrected core parser, five register cores and eight TCM banks
totalling 819,200 bytes (800 KiB). The bank selector is restored and verified.
CR4 is already halted (`IOCTL=0x21`, `RESET=0`); USB/ring/page tests still pass.
These observations authorize a narrow upload candidate for this chip profile.

This phase adds pinned vendor files and a manually invoked `wifiload` that
uploads firmware and board settings to **the Wi-Fi chip's volatile RAM**, then
compares every byte read back. The Wi-Fi CPU remains halted and D11 remains
in reset. This upload command never releases that CPU or installs its reset vector.
The separate [wifistart command](wifi-startup.md) now adds controlled startup;
physical startup now passes. The next [control-channel test](wifi-control.md)
adds read-only firmware queries. **Scanning, WPA3 and IP remain absent.**
Native mocks test the upload engine; real QEMU USB tests verify disk-file hashes.
QEMU has no Pi radio and cannot establish that the physical upload works.

## Verified physical upload (2026-11-09)

The [latest capture](../proofs/pi5-firmware-upload-verified-2026-11-09.txt)
confirms the corrected 64-byte F1 path: `phase=6 upload/readback=verified`,
code_bytes=609309, verified_bytes=609309, NVRAM_bytes=1748 at 0x25f92c,
CR4 halted and D11 reset. The token clear/readback and all file pins pass.
USB storagetest still matches 0x6a004021, pagetest passes and uptime reaches
210 seconds. This supersedes the failed-transfer results below. The next
startup test has now passed; see [startup evidence](wifi-startup.md) and the
next [read-only control test](wifi-control.md).

## Earlier failed transfer and correction

The [latest capture](../proofs/pi5-firmware-first-transfer-failure-2026-11-09.txt)
verifies all three file hashes and passes core/RAM discovery. wifiload reaches
phase 4 after confirming the halted CPU, resetting D11 and clearing/reading
back the NVRAM token. It then reports an I/O error at the first code chunk,
0x198000, with code_bytes=0 and verified_bytes=0. USB sector/hash/page tests
still pass and uptime advances. This is **not a successful firmware upload**.
The capture lacks the failing command's IRQ/R5 details, so the exact hardware
error cannot be determined from that log alone.

The [follow-up wifiinfo excerpt](../proofs/pi5-first-upload-r5-out-of-range-2026-11-09.txt)
now supplies last_irq=0x11 and last_response=0x1100. R5 has OUT_OF_RANGE (0x100);
the IRQ has command-complete and write-ready without host error bits. This
establishes a card rejection of the first request, rather than evidence of a
host CRC/timeout event. It still does not measure the exact byte-mode limit.

The corrected candidate configures and reads back F1's FBR block size as 64
before enabling the function, explicitly enables the host block count for
CMD53, and splits logical RAM calls into at most 64-byte wire requests. That
follows the reference driver's F1 setup and avoids a 512-byte byte-mode request
on this RAM path; it does not establish that this size alone caused the failure.
PIO polls for error bits after each word and stops immediately on error.

A retained first-failure snapshot now reports command/argument/direction, R5,
IRQ, present state, block-size/count, command/mode, requested/FIFO bytes and the
stage (command, buffer-ready, FIFO or transfer-end). Cleanup/repeat refusal
cannot overwrite it. The successful capture above now verifies upload with
these corrections. Never start firmware from the previous failed session.

## Test on the existing pendrive

Build or use the new complete header-console image:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Flash `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img` using the same
method and USB port as before. The complete image includes the radio files;
the separate boot-files-only ZIP does not. Keep the CP2102 connection and
115200 8N1 settings. On a fresh boot run individually:

```text
storageinit
wififiles
wifiinit
wificores
wifiload
wifiinfo
storagetest
storageinfo
pagetest
uptime
```

Use storageinit before usbtest/usbenum. `wififiles` only reads disk data and
checks lengths/SHA256; it performs no radio I/O. A successful check reports
`Wi-Fi firmware files: SHA256 pins verified` and all three hashes.

`wifiload` repeats the file checks before device writes, requires successful
wifiinit/wificores and the exact observed core/revision/RAM profile, and obtains
the board MAC from the firmware DTB. It can take a minute or more at the current
400 kHz SDIO clock; conservative 64-byte wire requests can take several minutes.
Wait for progress at each 64 KiB and the next prompt before
typing another command. Expected final output includes:

```text
Firmware readback verified 609309/609309 bytes
Wi-Fi firmware staging: ok phase=6 upload/readback=verified
CR4 last-confirmed halted=yes D11 held in reset=yes
code_bytes=609309 verified_bytes=609309 ...
Firmware is not executing. Next: controlled startup and FullMAC; no Wi-Fi connection.
```

Save all output, especially phase, result, last_address and SDIO host diagnostics
on failure. A repeated command after the first device write reports cached
status and requires reboot. If a file or MAC check fails before device writes,
the command does not take ownership of the radio. A partial upload never
deliberately starts the CPU; recovery is reboot, not an untested retry.

## Asset identity and notices

Pins are in [assets.json](../boot/wifi/assets.json), at RPi-Distro's
`firmware-nonfree` commit `c91cd2804cf7463aab913e7247c176049f16bbd6`:

| Disk file | Bytes | Source/use |
| --- | ---: | --- |
| FW43455.BIN | 609309 | `cypress/cyfmac43455-sdio-standard.bin`, uploaded unchanged |
| WIFINV.TXT | 2074 | `brcm/brcmfmac43455-sdio.txt`, Pi 5 board-alias target |
| WIFICLM.BIN | 2676 | `cypress/cyfmac43455-sdio.clm_blob`, verified on disk for later FullMAC use |
| WIFILIC.TXT | 313331 | Full upstream `debian/copyright`, retained unchanged |

Paths above are under `debian/config/brcm80211`, except the copyright file.
Pi 5 board aliases point to shared 43455 files; binary selection uses the
standard variant rather than the AP-client-tuned minimal variant. Raw GitHub
symlinks contain target path text, so the downloader pins the resolved targets.
Downloaded bytes were checked against Git blob IDs as well as SHA256. The
firmware is Cypress vendor code separate from the original OurOS kernel.
Retain WIFILIC.TXT and SOURCE.TXT; the Cypress terms permit binary redistribution
for its devices. The complete source/license notice is supplied rather than
silently assigning the project's original-code license to vendor firmware.
The board NVRAM is retained unmodified on disk. Runtime packing strips comments,
uses NUL-separated entries, replaces its example MAC with the DT MAC, pads to
four bytes and appends the documented length/complement token. No password,
SSID or country is inserted. Country/CLM/security-capability setup is later work
before RF activity; the loader does not start the radio.

## Driver contract and validation

- Exact chip gate: BCM4345 revision 6, AI backplane, five observed cores/revisions
  (CC 51, D11 54, CR4 9, PCIe2 14, SDIO 21), expected register/wrapper bases,
  eight banks, RAM base `0x198000`, size `0xc8000`, restored selector.
- Verify all three input files before changing D11. Check live CR4 halt/clock
  and reset state. Preserve that CPU state; disable only D11 using its AI
  wrapper's clock/reset sequence with bounded waits and readback.
- Configure/read back F1's 64-byte FBR block size before function enable.
  Transfer function-1 incrementing CMD53 byte-mode requests of 4..64 bytes,
  splitting 4..512-byte API calls at that limit and 32 KiB windows. The host
  programs one block and explicitly enables its block counter, without DMA,
  multi-block or automatic stop commands. Address/alignment/length gates restrict
  RAM access to `[0x198000,0x260000)`. No function-2 or RF frame transfers.
- Clear the NVRAM length token first. Upload code at RAM base, padding only
  the last transfer with three zero bytes, and compare each chunk read back.
  Recompute the source SHA256 during upload as well as during preflight.
- Pack NVRAM into a bounded 4 KiB buffer and place it at RAM end. Verify its
  payload before committing and verifying the final token. Verify CR4/D11
  states again. A full chip-RAM SHA256 pass after payload writes and before
  token commit checks persistence across windows and overlap with NVRAM.
  On failure, attempt to clear the token if previously cleared;
  a transport failure may prevent cleanup, so reboot is required.
- Binary FAT reads use an opened-file cluster index with a complete bounded
  chain/mirror check once, avoiding repeated full-chain walks. The monitor's
  64 KiB cat cap remains. No heap, filesystem writes or physical disk writes.
- SHA256 tests cover known vectors, padding and one million `a` bytes; actual
  assets match pins. Native loader tests check padded readback, MAC/NVRAM/token,
  profile gates, reset refusal, I/O/mismatch/timeouts, deliberate 32 KiB window
  aliasing, cleanup and repeat refusal.
  FAT tests compare the full large firmware to the independently pinned cache.
- Native PIO tests write then read all 800 KiB through the actual SDIO/backplane
  engine using a window-translating model that rejects RAM requests over 64
  bytes. Additional cases cover FBR readback mismatch, a mid-FIFO CRC error,
  timeout stages and first-snapshot preservation. This modeled size constraint
  is a regression guard, not a measurement of the physical card's limit.
  The captured R5=0x1100/IRQ=0x11 response is reproduced on a raw 512-byte request
  and stops before FIFO writes; the split RAM path completes in that model.

Primary references: [firmware/board assets](https://github.com/RPi-Distro/firmware-nonfree/tree/c91cd2804cf7463aab913e7247c176049f16bbd6/debian/config/brcm80211),
[Cypress notices](https://github.com/RPi-Distro/firmware-nonfree/blob/c91cd2804cf7463aab913e7247c176049f16bbd6/debian/copyright),
[core halt/reset sequence](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c),
[firmware/NVRAM placement](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c),
[NVRAM packing/token](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/firmware.c),
[F1 block-size setup](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c),
[SDHCI PIO and transfer-mode setup](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/mmc/host/sdhci.c),
[SDIO R5 error definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/include/linux/mmc/sdio.h),
[SHA256 definition](https://csrc.nist.gov/pubs/fips/180-4/upd1/final).
