# Pi 5 header UART boot observed: 2026-11-08

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

The new MB/MiB display and [RAM filesystem](ramfs.md) are a subsequent revision
tested on the host/QEMU and still awaiting their own physical capture.

The default embedded firmware Arm stub is sufficient for this observed boot;
the missing custom armstub and cmdline messages did not prevent the monitor
from starting in this captured run.
