# Real Raspberry Pi 5 proof

The Pi and Windows passed a live 9-byte TLS 1.3 encrypted echo. Windows
verified client Finished, and the Pi exited with code 0 and restored
app resources. [Live test result](../docs/tls-live.md#local-verification)

The Pi passed the enrolled OpenSSL TLS 1.3 transcript with code 0.
Unenrolled, wrong-name, changed ClientHello and changed Finished cases
returned code 1 with resources restored. [Pi TLS transcript capture](pi5-tls-transcript-2026-11-22.txt)

With UTC set, the Pi rejected the demo certificate before pin enrollment,
passed it with exit code 0 after enrollment, rejected the wrong DNS name,
and cleared its trust anchors. [Pi certificate capture](pi5-pinned-cert-2026-11-22.txt)

Operator-set UTC advanced on the Pi, the EL0 clock test exited with code
0, and trust preflight accepted the valid window and rejected the expired
one. [Pi UTC and trust capture](pi5-trust-time-2026-11-22.txt)

The Pi and a Windows PC echoed a 16 KiB file over TCP. Both reported
the same SHA-256 and `TCPCHK.ELF` exited with code 0.
[Pi and Windows capture](pi5-tcp-echo-2026-11-22.txt)

The Pi and a Windows PC exchanged a 10-byte UDP datagram over the local
Wi-Fi network. `UDPCHK.ELF` verified the echoed payload and exited with
code 0. [Pi and Windows capture](pi5-udp-echo-2026-11-22.txt)

The corrected Pi build passed shell help, two-job control, terminal handoff,
command lists and `source`. The watchdog reboot remounted USB storage, retained
generation 14 and the saved file's SHA-256, and passed the timer test.
[Passing capture](pi5-safe-reboot-2026-11-22.txt) · [First capture](pi5-shell-workflows-2026-11-22.txt)

The Pi delivered two BCM compare interrupts and ran two deferred callbacks.
The periodic timer, EL0 preemption, FAT reads and snapshot bank checks passed.
[Peripheral IRQ capture](pi5-peripheral-irqs-2026-11-22.txt)

The Pi distinguished a full private quota from fragmentation and recovered
all private pages. The physical-page, kernel heap and file-handle counts
matched before and after. [Memory pressure capture](pi5-memory-pressure-2026-11-21.txt)

The installed 10,664-byte ELF still ran after a power cycle on the SanDisk
drive. The 16 KiB file retained its SHA-256 and the mounted generation
remained 14. Windows verified the earlier generation 10 serial export.
[Pi persistence capture](pi5-v4-persistence-2026-11-21.txt)

The Pi passed `HEAPCHK.ELF resize` with child exit 0. The test checks
in-place growth, preserved contents, zeroed new bytes and tail-page
reclamation. [Pi capture](pi5-heap-resize-2026-11-21.txt)

The open-ended shell responded after idling, gave another prompt after each
Ctrl+C, and exited with code 0 and resource cleanup. [Pi capture](pi5-open-shell-2026-11-21.txt)

The SanDisk v4 volume passed `datacheck` on the Pi. Both generations were
valid and generation 11 matched the mounted filesystem. [Pi capture](pi5-datacheck-2026-11-21.txt)

The SanDisk boots directly into the EL0 shell, `df /data` reports v4,
and `exit` returns to the monitor. `V4CHK.ELF` passes its 16 KiB file
checks with exit 0 and restored resources. [Pi capture](pi5-startup-v4chk-2026-11-21.txt)

The [v4 mount capture](pi5-data-v4-mount-2026-11-21.txt) shows the SanDisk
Cruzer Blade, a read-only FAT boot mount, the v4 data partition and a successful
`DF.ELF` run with restored resources. It records a manual boot.

The latest [shell-job excerpt](pi5-shell-jobs-2026-11-15.txt) verifies one
background SPIN child, responsive parent input/echo, stop/collection with 130,
an empty job record and shell exit with code 0/resources restored. Its scope
does not include fg/bgio or reboot/power-loss behavior.

The latest [process-control capture](pi5-process-control-2026-11-15.txt) passes
PS check (12 children collected) and the interactive stop/start demo, with
code 0 and resources restored. Shell jobs use that foundation; their narrower
physical evidence is recorded above.

The latest [byte-allocator capture](pi5-user-allocator-2026-11-15.txt) passes
HEAPCHK and HEAPDEMO with code 0, zero live pages and restored resources.
Its scope is these two runs. The earlier
[dynamic-page capture](pi5-user-memory-2026-11-15.txt) passes MEMCHK and MEMDEMO.
The [current status](../docs/os-status.md) links earlier MMU, timer, process,
filesystem and networking evidence and tracks the remaining implementation.

Original PuTTY screenshots supplied by the project author on 8 November 2026.
They are preserved unchanged and displayed in the main README.

| File | Evidence |
| --- | --- |
| [15:43:10](Screenshot%202026-11-08%20154310.png) | Firmware UART setup, board revision and image loading |
| [15:43:52](Screenshot%202026-11-08%20154352.png) | OurOS banner, EL2 entry, EL1 execution and monitor startup |
| [15:44:07](Screenshot%202026-11-08%20154407.png) | Commands, advancing uptime, echo, CPU ID and expected RAM-discovery limitation |
| [17:41:24](Screenshot%202026-11-08%20174124.png) | Physical RAM discovery, page patterns, allocations and double-free rejection |
| [18:14:54](Screenshot%202026-11-08%20181454.png) | MB/MiB summaries and real-board RAMFS write/list/read/navigation |

The timestamp labels come from the filenames; they are not independently
verified measurement timestamps. The GIF in `docs/assets/` is a separately
labelled illustration. See [the verification record](../docs/pi5-hardware-verification.md)
for the tested setup and remaining limits.
