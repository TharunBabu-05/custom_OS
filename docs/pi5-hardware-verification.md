# Pi 5 hardware evidence: header UART and incremental driver tests

Evidence: serial output supplied by the user from their physical Raspberry Pi 5
and CP2102 adapter. This records the observed boot milestone; it is not a claim
of comprehensive hardware or OS validation.

## Tested setup

- Header-console build: `rpi5-header`, RP1 UART0 at `0x1c00030000`.
- Physical header pins 8/10/6 for TX/RX/ground, 115200 8N1, no flow control.
- Earlier firmware capture reported board revision `d04171`; exact EEPROM
  version and printed board/RAM details have not been recorded.
- Firmware reads OUROS.IMG (8,105 bytes) and PI5.DTB (78,770 bytes), loading
  them at `0x80000` and `0x200000` respectively.
- Associated local image: `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
  Its build-time SHA256 is
  `175ad63ccf32c0d4e4e9be30057f88cdc5044c6d59a20c74cf4c07d04c5ab6d6`.
  The user did not provide an independent readback hash from the flashed medium.

## Observed kernel output

Verbatim kernel portion of the user's capture:

```text
OurOS: AArch64 kernel booted
Firmware configured PL011 at 0x0000001c00030000 (baud/clock preserved)
Board=rpi5-header entered EL2 running EL1
Kernel monitor ready. Type help.
> help
help info cpuinfo meminfo uptime echo <text> clear halt fault
> info
OurOS development kernel | board=rpi5-header | EL1 | entered EL2
Firmware DTB pointer=0x0000000000200000
Single core; polling UART; MMU off; no user processes or filesystem.
> uptime
uptime_seconds=26
> uptime
uptime_seconds=29
> echo

> this
Unknown command. Type help.
> echo pi5-ok
pi5-ok
> cpu info
Unknown command. Type help.
> cpuinfo
MIDR=0x00000000414fd0b1 timer_hz=54000000
> meminfo
RAM discovery not implemented for this board; no allocator active.
>
```

This establishes real-board kernel startup, reported EL2 entry and EL1 execution,
DTB pointer handoff, bidirectional polling UART, monitor command dispatch,
unknown-command handling, echo and an advancing physical counter. The two
uptime values establish advancement, not calibration against an external clock.
An empty `echo` correctly prints an empty line. The user's expanded capture
also confirms CPU identification (`0x414fd0b1`), a 54 MHz counter frequency,
and the expected unimplemented Pi RAM-discovery message. Original screenshots
are preserved in [proofs/](../proofs/).

## Remaining checks and limitations

- Capture externally timed uptime advancement. Repeat a full power cycle and
  capture the same commands; repetition was not established by this session.
- Record EEPROM version, physical model/RAM and image readback identity when
  available. Other firmware mappings, boards and the dedicated UART10 build
  have not been physically verified by this capture.
- Missing D0 overlay remains a device-tree packaging gap; the current polling
  console does not consume its GPIO/interrupt/DMA descriptions. This successful
  boot does not validate those peripheral descriptions.
- At this original monitor milestone, no MMU, allocator, scheduler, target
  filesystem, HDMI, USB keyboard, networking or user processes were implemented.
  Build-time artifact labels reflect their
  creation before this hardware observation and do not encode per-board test
  history. The kernel/image bytes were not changed to record this milestone.

A subsequent revision added [RAM discovery and physical pages](memory.md).
The user's later screenshot confirms that revision on the physical Pi, as
recorded below. The verbatim text earlier in this document remains the original
8,105-byte monitor kernel's output.

## Physical memory milestone observed

The user supplied a PuTTY screenshot showing the following memory test results:

| Item | Observed value |
| --- | --- |
| Firmware-described RAM | 2 banks, 8,585,740,288 bytes (8188 MiB) |
| Managed RAM | 1,835,008 pages, 7,516,192,768 bytes (7168 MiB) |
| Reservations | 6, including the conservative low 1 GiB dynamic window |
| Four-page pattern test | PASS, pages written/read/freed and counters restored |
| First monitor allocation | Slot 0 at `0x40000000` |
| Second monitor allocation | Slot 1 at `0x40001000` |
| Allocated/free counters | 0 / 1,835,008, then 2 / 1,835,006, then restored |
| Repeated free | Rejected as an invalid or unallocated slot |

Associated local memory image SHA256:
`06cf730bab6a4b3c43f712061c43390417ee126ec911442b18745e5d52b3671b`.
No independent readback hash was supplied. This baseline is preserved locally
as `build/rpi5-header/OurOS-Pi5-header-memory-verified.img`.

## RAMFS and MB/MiB milestone observed

The author's [18:14:54 screenshot](../proofs/Screenshot%202026-11-08%20181454.png)
confirms the subsequent RAMFS and readable-memory revision. It shows:

- Reported 8585.740 MB (8188.000 MiB), managed 7516.192 MB (7168.000 MiB).
- `/README.txt` content and the initial three data-page allocations.
- Navigation to `/home/user/notes`, writing `hello.txt`, listing its 16-byte
  content, and `cat` returning `hello from OurOS`.
- `pwd` returning `/home/user/notes`, and fsinfo showing four files/seven
  directories/four data pages after the user's additions.
- Expected invalid-path rejection for a backslash-suffixed path.

Associated build image SHA256:
`62574b624ebebf2b363aca6be50467694d01364326f3509c3e112f5f0c726ea6`.
The local baseline is preserved as
`build/rpi5-header/OurOS-Pi5-header-ramfs-verified.img`.
The [17:41:24 screenshot](../proofs/Screenshot%202026-11-08%20174124.png)
also preserves the earlier memory-test evidence. Filenames provide author
timestamps, not independently verified measurement times or readback hashes.

The user subsequently supplied a physical text capture for the storage revision:
the expanded RAM root boots and lists /boot, /dev, /proc, /sys, /var/log and /mnt;
navigation and /etc/board reads work. wifiinfo reads the actual firmware node,
compatible family binding and MAC. The user confirms **all these boots used a
32 GB USB pendrive, with no microSD card installed**. Firmware USB boot is thus
observed, while kernel USB storage is not implemented.

In that capture storageinit returns unsupported controller/card state from
SDHCI 0x1000fff000 with control1=0, last_irq=0 and last_response=0. This is an
expected failed SD-handoff attempt in the USB-only setup, not a successful SD
or FAT32 mount. /boot stays empty and CONFIG.TXT is unavailable. The SD/FAT read
path and Wi-Fi CMD5 query still need their own applicable physical tests.
The subsequent [USB discovery text capture](../proofs/pi5-usb-discovery-2026-11-08.txt)
confirms both RP1 host windows are readable at 0x1c00200000 and 0x1c00300000.
Each reports xHCI 1.10, 64 slots, 4 interrupters, 3 root ports, 64-bit addresses
and 64-byte contexts. Both controllers are running; usb0 port 1 is connected,
enabled and powered (PORTSC 0x200e03, speed ID 3, protocol 0x0200); other ports
are disconnected. Uptime advances from 12 to 14 after probing. No USB device
descriptor, capacity or file read is present in this capture.

The associated local discovery image hash is
80d84ba2e22f1b38e1d5471ff6dacf4322f6267b5f9f1d0fbb4feeb672b55bbe;
the local baseline is OurOS-Pi5-header-usb-discovery-observed.img. This is not
an independently measured hash of the physical pendrive. The new usbtest DMA
and wifiinit SDIO paths still require physical captures. WPA3-Personal was
selected as the network target; no Wi-Fi association has been observed.

## Cold Wi-Fi and firmware DMA profile observed (2026-11-09)

The user's [full serial capture](../proofs/pi5-cold-wifi-and-dma-preflight-2026-11-09.txt)
boots the 61,840-byte kernel from USB and reports:

- Wireless CONTROL0=0x00800000, CONTROL1=0, PRESENT=0x000f0000,
  CAPS=0x55eec832 and CQE=0. wifiprobe/wifiinit return device not ready at
  phase 0; no SDIO response, device identity or connection is established.
- Both USB hosts remain accessible. The connected root port is now usb1
  port 1 (PORTSC=0x200e03); usb0 has no connected ports in this capture.
- Live PCIe BAR2=0x15, UBUS_REMAP=1. The previous DMA decoder refuses that
  identity mapping before changing the controller. There is no Pi No-op
  completion in this log.
- The monitor remains responsive and uptime reaches 67 seconds.

Associated local image hash:
23f8f40caab0fa78bc07e1e5aa5fc8055e21f2f0efc7ebbd86068bc50e1682c9.
Backup: build/rpi5-header/OurOS-Pi5-header-dma-preflight-observed.img.
This remains an associated local artifact, not a physical-drive readback hash.

The next revision adds the observed DMA profile, connected-host selection and
step-specific WLAN cold preparation. Native and emulator checks do not establish
that those changes work on the physical Pi; a new capture is required.

## WLAN selection and USB DMA observed (2026-11-09)

The next [full serial capture](../proofs/pi5-wlan-selected-usb-dma-pass-2026-11-09.txt)
boots the 65,976-byte kernel and confirms:

- SoC stepping 0x27120030 (D0), board preparation phase 6 and configured
  SD_clock_hz=400000, CONTROL0=0x00800f00 and CONTROL1=0x000efa07.
- SDIO initialization reaches phase 7. R4=0xb0ffff00, RCA=1, three functions,
  CCCR=3, SDIO revision=4, CIS vendor=0x02d0 and device=0x4345.
- The optional CMD53 function-0 CIS probe then fails with R5=0x1100. The
  software marks the attachment unselected. This is a later transport-probe
  failure after successful power-up/selection/CMD52 identity, not a Wi-Fi link.
- usbtest selects usb1, uses the firmware identity DMA window and completes
  four No-op commands. It reports two scratchpads, command DMA=0x195000,
  event DMA=0x196000, completion code 1, pointer=0x195030 and halted=yes.
- The monitor and counter remain responsive, reporting uptime=90 afterward.

Associated local image hash:
245d6176174dddac8c3100122c9f8bba568ce1951640a3b926190c799f8edc95.
Backup: build/rpi5-header/OurOS-Pi5-header-wlan-dma-observed.img.
No independent physical-drive readback hash was supplied. Configured SD clock
frequency is inferred from source/divider state, not an external measurement.

The next build removes that optional common-CIS probe, adds guarded Broadcom
function-1/ALP/backplane identification and direct USB2 device descriptor reads.
Those changes require new captures; no pendrive file reads, RF association or
WPA3/internet connection has been observed.

## ChipCommon identity and USB2 enumeration observed (2026-11-09)

The latest [full serial capture](../proofs/pi5-chip-id-and-usb-descriptors-2026-11-09.txt)
boots the 74,184-byte kernel from the pendrive and confirms:

- D0 WLAN preparation and SDIO selection still pass. Function-1 backplane
  identification now reports ok, selected=yes, clock CSR=0x61.
- Raw ChipCommon ID=0x15264345, chip=0x4345, revision=6. These are live readings,
  unlike the native tests' mock chip-word fixture.
- usbenum selects usb1 using the identity DMA window, completes four No-ops
  with two scratchpads, addresses port 1/slot 1 and reads USB2 descriptors.
- VID=0x0781, PID=0x5567, bcdUSB=0x0200, high-speed ID=3, EP0 packet=64,
  configuration=1, BOT interface=0, IN=0x81/OUT=0x02, both packets=512.
- Controller halt, uptime=54, the physical page self-test and one-shot refusal
  after enumeration are observed.

Associated local whole-image SHA256:
d873a1df2e7ee68a0b657ea78af19c67c4f99de4fd903af02b4b1ebed62fde8d.
Backup: build/rpi5-header/OurOS-Pi5-header-chip-usb-enum-observed.img.
This is not a physical pendrive readback hash. No USB sector/file reads,
Wi-Fi firmware startup, WPA3 association or IP packets appear in this capture.

The next image activates USB bulk endpoints, implements read-only BOT/SCSI
and attempts FAT32 at /boot. Native and QEMU checks validate this path, but
the new Pi storage path requires a fresh capture. Do not infer disk-read
success from the already observed descriptor milestone.

The default embedded firmware Arm stub is sufficient for this observed boot;
the missing custom armstub and cmdline messages did not prevent the monitor
from starting in this captured run.

## USB BOT capacity, FAT32 files and ring wrap observed (2026-11-09)

The [new full serial capture](../proofs/pi5-usb-fat-and-ring-wrap-2026-11-09.txt)
boots the 83,016-byte kernel from the pendrive and runs storageinit first:

- usb1, VID 0781/PID 5567, BOT interface 0, IN 81/OUT 02, 512-byte packets.
  Configuration and bulk endpoints activate successfully.
- SCSI vendor SanDisk, product Cruzer Blade; sense key/ASC/ASCQ all zero.
  READ CAPACITY reports 30,765,219,840 bytes (29,340 MiB). This is device capacity,
  not the FAT volume size or a physical full-drive readback measurement.
- /boot mounts FAT32 read-only at LBA 2048, 260096 sectors, 255968 clusters.
  ls lists the seven actual files and their sizes; CONFIG.TXT contains the
  expected header-console settings and kernel=OUROS.IMG.
- storagetest reads sectors 0..511 and reports FNV-1a32 0x6a004021, matching the
  host check of the associated image. IN/OUT/event wrap counters reach 4/2/6.
- CONFIG.TXT is read again after wrapping. Subsequent wifiinit reports chip
  0x4345 revision 6 and the page self-test passes. No disk I/O error or fatal
  exception is present in the supplied run.

Associated local whole-image SHA256:
5238ff93142e7dfa855421e9965e50ac3c64cecd1ef88a7d70603675e929d16f.
Backup: build/rpi5-header/OurOS-Pi5-header-usb-fat-observed.img.
The FNV test covers 256 KiB; no full physical drive SHA256 readback was supplied.
The 128 MiB partitioned image does not use the drive's remaining space.

The new wificores command reads the radio's AI core table and queries CR4 RAM
banks, saving/restoring the bank selector. Native tests validate parsing and
failure handling, but this new core/RAM path still needs physical evidence.
Firmware upload/startup, WPA3 association and IP networking remain unfinished.

## Core discovery stopped at an untagged size payload (2026-11-09)

The [next serial capture](../proofs/pi5-erom-size-field-rejection-2026-11-09.txt)
boots the 87,612-byte kernel. USB/FAT mounts again, wifiinit still identifies
0x4345 revision 6, and wificores reads EROM at 0x1810a000. It stops after six
words with core_map=unverified/RAM=unavailable, cores=0, last_address=0x1810a014:

```text
4bf80001 33004411 00000003 18000005 1c000135 01000000
```

The last word is an untagged size payload for the preceding address region.
Our previous descriptor-tag check wrongly rejected it. This was a parser
failure before the CR4 RAM-bank query; no bank writes or CPU/D11 reset occurred.
SDIO remains selected=yes, repeated wificores uses cached results, storagetest
still matches 0x6a004021 with wraps 4/2/6, pagetest passes and uptime reaches 105.
Repeat storageinit reuses /boot; usbtest/usbenum correctly refuse takeover of
the active storage session. halted=not-confirmed here is expected because the
successful storage controller remains running, not an attempted halt failure.

Associated local image SHA256:
97db6c6220891049f01205f5bef3cea7ba7185da68551911269bde9c0cca07a7.
Backup: build/rpi5-header/OurOS-Pi5-header-core-erom-size-observed.img.
No physical-drive readback hash was supplied. The corrected parser now consumes
raw size payloads; native tests replay the captured prefix with a synthetic
continuation. Full physical core discovery and RAM sizing still need a capture.

## Wi-Fi core map and 800 KiB TCM observed (2026-11-09)

The [new full capture](../proofs/pi5-wifi-cores-800k-ram-2026-11-09.txt) confirms
the raw-size parser fix on the 87,612-byte kernel. EROM=0x1810a000, 88 words,
five cores, core_map=valid and RAM=measured:

| Core ID | Revision | Register base | Wrapper |
| --- | ---: | --- | --- |
| 0x800 ChipCommon | 51 | 0x18000000 | 0x18100000 |
| 0x812 D11 | 54 | 0x18001000 | 0x18101000 |
| 0x83e ARM CR4 | 9 | 0x18002000 | 0x18102000 |
| 0x83c PCIe2 (logged as other) | 14 | 0x18003000 | 0x18103000 |
| 0x829 SDIO | 21 | 0x18004000 | 0x18104000 |

CR4 IOCTL=0x21, RESET=0, capability=0x0b44, eight banks. Reported bank sizes
are 131072, 131072, 131072, 32768, 131072, 65536, 131072 and 65536 bytes,
totalling 819200 bytes / 800 KiB. The selector is restored and verified. The
base 0x198000 is the chip-map selection; size comes from bank registers.
The new staged loader requires this exact observed profile before writes.

USB/FAT attachment, 512-sector hash 0x6a004021, pagetest and advancing uptime
(39 seconds) still pass afterward. No firmware upload or network packets are
present in this capture. Associated local image SHA256:
01074e919a4f931251670eedcfa0f65c321be1168e3680abe59705615c3489c1.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-cores-ram-observed.img.
No physical-drive readback hash was supplied.

The next build adds pinned vendor assets and upload/readback with CR4 halted
and D11 in reset. Native and QEMU disk tests do not prove physical SDIO bulk
upload; a fresh capture is required. Firmware startup/WPA3/IP remain later work.

## Firmware file pins pass; first code transfer fails (2026-11-09)

The [next serial capture](../proofs/pi5-firmware-first-transfer-failure-2026-11-09.txt)
boots the 118,620-byte kernel. USB/FAT, all three runtime firmware SHA256 pins,
the same five cores, 800 KiB RAM and restored selector pass. wifiload verifies
the files again, confirms CR4 halted and D11 reset, and prepares 1748 packed
NVRAM bytes at 0x25f92c. It reaches phase=4 after the initial token clear/readback,
then returns an I/O error at 0x198000 with code_bytes=0, verified_bytes=0.
The upload did not pass and no firmware was started.

The SDIO error disables the attachment and cached retry refusal works. Host
PRESENT=0x01f70506 is recorded; the log does not include the failing command's
IRQ or R5, so an exact CRC/card/host cause is not established. USB storagetest
still matches 0x6a004021, pagetest passes and uptime reaches 154 seconds.

Associated local image SHA256:
cd89d288476ab8a8fe05486ec45d3cf4d64ee9b33ea4febc9bf66c9beb8048ca.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-upload-first-chunk-observed.img.
No physical-drive readback hash was supplied.

The new candidate configures F1's 64-byte block size with readback, enables host
block counting, splits RAM traffic into <=64-byte wire requests, polls errors
during PIO and preserves first-failure command/argument/response/IRQ/present/
block/mode/byte/stage diagnostics. Native tests validate the protocol changes
and full-window memory model; physical upload success remains unverified.

The user's [follow-up wifiinfo excerpt](../proofs/pi5-first-upload-r5-out-of-range-2026-11-09.txt)
adds last_irq=0x11 and last_response=0x1100 from the same failed session. R5
contains OUT_OF_RANGE (0x100). The host IRQ contains command-complete and
write-ready without CRC/timeout error bits. The narrower diagnosis is a card
rejection of that request; the exact maximum byte-mode length is not measured.
This supersedes the original log's missing-IRQ uncertainty above.

## Complete firmware upload/readback passes (2026-11-09)

The [latest capture](../proofs/pi5-firmware-upload-verified-2026-11-09.txt)
boots the 118700-byte kernel and verifies the corrected F1/PIO path. All three
disk-file pins pass. wifiload reaches phase=6 upload/readback=verified with
code_bytes=609309 and verified_bytes=609309. Packed NVRAM is 1748 bytes at
0x25f92c. The initial token clear/readback succeeds; CR4 remains halted and
D11 held in reset. The full persistent-RAM code hash is part of this success
path. Firmware was intentionally not started in this capture.

USB storage still reads 512 sectors with FNV1a32=0x6a004021, pagetest restores
its counters and uptime reaches 210 seconds. This supersedes the earlier
upload failure and establishes upload, not execution, scan or association.
Associated local image SHA256:
46ea45ad2bb1070aa13404943ce2c902e12832745f39dcfe87d50ffe5ceabf77.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-upload-verified.img.
No physical-drive readback hash was supplied.

The next candidate adds [wifistart](wifi-startup.md): verified staging gates,
single-word reset-vector installation, bounded CR4 reset/release and HT/shared
memory validation. Native tests pass, including per-operation failures and
malformed/crashed firmware states. Physical startup remains UNTESTED; function
2, FullMAC, WPA3 and IP remain unfinished.

## Firmware startup/shared memory passes (2026-11-09)

The [latest serial capture](../proofs/pi5-firmware-startup-verified-2026-11-09.txt)
boots the 122852-byte kernel. Upload again verifies all 609309 code bytes and
1748 packed NVRAM bytes. wifistart then reaches phase=7 shared_memory=ready:
vector 0xb83ef198, HT CSR 0xd0, shared pointer 0x201cc0, flags 1, console
0x25debc, trap 0 and assert line 0. The pointer is published on the first poll.
Mailbox 0 is recorded before F2 enable. Old CR4 halt/D11 reset state is clearly
reported as historical after release, rather than a current passive state.

USB storagetest matches 0x6a004021, pagetest restores counters and uptime
reaches 144 seconds. This confirms firmware boot initialization, not a FullMAC
control reply, scan, WPA3 association or IP access.
Associated local image SHA256:
df26b379821a50d3925a7c26152bd783a683f61f41dc631b10233ad7edc47a24.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-startup-verified.img.
No physical-drive readback hash was supplied.

The next candidate adds [wificontrol](wifi-control.md): F2 enable/readiness,
SDPCM mailbox protocol/credits and two read-only BCDC firmware queries.
Native tests pass; the physical packet/control path remains UNTESTED.

## Firmware control/version queries pass (2026-11-09)

The [latest capture](../proofs/pi5-wifi-control-verified-2026-11-09.txt) boots
the 133716-byte kernel. Upload and startup repeat successfully. wificontrol
reaches phase=8 queries=verified: F2 block size 512, IO_ENABLE/IO_READY 6,
HT CSR 0xd2, mailbox 0x40002 (protocol 4 + DEVREADY). Two TX and four RX frames
include two drained events; sequence resync remains zero. Returned ioctl ABI
is 2 and firmware version is 7.45.265 (28bca26 CY), FWID 01-b677b91b, build
timestamp Aug 29 2023 01:47:08. These are actual chip replies.

USB storagetest still matches 0x6a004021, pagetest passes and uptime reaches
192 seconds. This confirms firmware control communication, not advertised
SAE/PMF support, CLM/country configuration, association or internet access.
Associated local image SHA256:
f5ba5932dc934e6229e55b8825ebd6c318cc8d2ec927c74650cc10e0c6ebd11c.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-control-verified.img.
No physical-drive readback hash was supplied.

The next [capability/regulatory candidate](wifi-prepare.md) adds wificaps and
wifiprepare IN. The user confirmed IN (India). Native tests pass; the larger
control transfers, CLM loading and country readback remain UNTESTED on the Pi.
No scan or association request is implemented in this revision.

## Capability and CLM/country setup pass (2026-11-09)

The [latest serial capture](../proofs/pi5-wifi-prepare-verified-2026-11-09.txt)
boots the 148412-byte kernel and repeats upload/startup/control success.
wificaps reaches phase 5 with the actual MAC 2c:cf:67:f4:99:75. The capability
list contains sae_ext and mfp, but not the firmware SAE token. mfp GET returns
1; sup_wpa returns firmware UNSUPPORTED (-23). The transport remains ready.

wifiprepare IN reaches phase 6: the pinned 2676-byte CLM uploads in two chunks,
clmload_status is zero, CLM version reports API 12.2 / Data RPi.4, and country
and ccode IN are read back with revision 0. Control totals are TX 12/RX 14,
two events, zero resync. USB storagetest matches 0x6a004021, pagetest passes
and uptime reaches 150 seconds. No scan or association was requested in that
capture. External SAE/PMF exposure does not establish an implemented WPA3 path.

Associated local image SHA256:
bc3f693557eeb3b6e4a302fddd5845942b1da40d33c80b8b315f08f341c42eab.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-prepare-verified.img.
No physical-drive readback hash was supplied.

The next [passive scan candidate](wifi-scan.md) adds event subscription,
interface UP, passive escan and bounded BSS/security parsing. Native tests
pass; physical scanning remains UNTESTED. Association/WPA3/IP are unfinished.

## Passive scan finds the WPA3 hotspot (2026-11-09)

The [latest capture](../proofs/pi5-wifi-scan-wpa3-hotspot-2026-11-09.txt)
boots the 160372-byte kernel and repeats successful firmware/control/CLM/IN
setup. Passive escan v1 completes with five networks, eleven matching events,
five duplicate observations, malformed=0, ignored=1 and overflow=0. Interface
DOWN and event-mask restoration/readback pass.

Jack's is now observed at BSSID e6:49:0c:b1:bc:23, channel 6, 2.4 GHz,
RSSI -28 dBm, RSN+SAE and PMF required. Other observations span channels 1,
6, 10 and 11. The earlier absence does not establish a WPA3 scan filter;
this capture verifies that SAE advertisements are parsed and displayed.
USB storagetest still matches 0x6a004021, pagetest passes and uptime is
303 seconds. This is discovery evidence, not authentication or IP access.

Associated local image SHA256:
99a7a861b6958ee8befa2a392f3bce6fd4fc33912b6b6ff57d4c0aed9cd3ef41.
Backup: build/rpi5-header/OurOS-Pi5-header-wifi-scan-verified.img.
No physical-drive readback hash was supplied.

The next [authentication-foundation candidate](wifi-auth-foundation.md)
adds HMAC/HKDF known-answer tests and a manually invoked, DT-gated RNG200
source test. Host/native tests pass; the physical RNG path remains UNTESTED.
No credentials are consumed and no association request is implemented.
